#include "App.hpp"
#include "StatusBar.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifdef PLATFORM_WINDOWS
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  undef RGB
#endif

using namespace ftxui;

namespace diskscan {

namespace {

#ifdef PLATFORM_WINDOWS
std::string driveTypeName(UINT type) {
    switch (type) {
        case DRIVE_FIXED: return "Fixed";
        case DRIVE_REMOVABLE: return "Removable";
        case DRIVE_REMOTE: return "Network";
        case DRIVE_CDROM: return "Optical";
        case DRIVE_RAMDISK: return "RAM";
        default: return "Drive";
    }
}

std::vector<PickEntry> listDriveRoots() {
    std::vector<PickEntry> entries;
    DWORD mask = GetLogicalDrives();

    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if ((mask & (1u << (letter - 'A'))) == 0) continue;

        std::string root;
        root += letter;
        root += ":\\";

        char volumeName[MAX_PATH + 1] = {};
        GetVolumeInformationA(root.c_str(), volumeName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);

        auto type = driveTypeName(GetDriveTypeA(root.c_str()));
        std::string name = root + "  " + type;
        if (volumeName[0] != '\0') {
            name += "  ";
            name += volumeName;
        }

        entries.push_back({name, root, false});
    }

    return entries;
}
#endif

std::string displayNameForPath(const std::filesystem::path& path) {
    auto name = path.filename().string();
    return name.empty() ? path.string() : name;
}

} // namespace

App::App(std::string rootPath)
    : rootPath_(std::move(rootPath))
    , screen_(ScreenInteractive::Fullscreen())
{}

App::~App() {
    if (scanner_) {
        scanner_->abort();
        scanner_.reset();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Scan lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void App::startScan() {
    scanner_ = std::make_unique<Scanner>(rootPath_);
    scanner_->start([this](const ScanProgress& p) {
        onScanProgress(p);
    });
}

void App::onScanProgress(const ScanProgress& p) {
    {
        std::lock_guard<std::mutex> lock(progressMtx_);
        progress_ = p;
    }

    if (p.done) {
        auto result = p.errorMessage.empty() ? scanner_->result() : nullptr;
        // Post back to UI thread
        screen_.Post([this, result = std::move(result)] {
            root_ = result;
            treeView_.setRoot(root_);
            if (root_) {
                statusBar_.setSelected(root_->path, root_->sizeBytes);
            }
        });
    }

    // Trigger a redraw on the main thread
    screen_.PostEvent(Event::Custom);
}

// ─────────────────────────────────────────────────────────────────────────────
// Path picker
// ─────────────────────────────────────────────────────────────────────────────

void App::initializePicker() {
    mode_ = ViewMode::Picker;
#ifdef PLATFORM_WINDOWS
    pickerCurrentPath_.clear();
#else
    pickerCurrentPath_ = "/";
#endif
    refreshPicker();
}

void App::refreshPicker() {
    pickerError_.clear();
    pickerEntries_.clear();

#ifdef PLATFORM_WINDOWS
    if (pickerCurrentPath_.empty()) {
        pickerEntries_ = listDriveRoots();
        pickerCursor_ = std::clamp(pickerCursor_, 0, std::max(0, (int)pickerEntries_.size() - 1));
        return;
    }
#endif

    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path current(pickerCurrentPath_);

    auto parent = current.parent_path();
    if (!parent.empty() && parent != current) {
        pickerEntries_.push_back({"..", parent.string(), true});
    }

    std::vector<PickEntry> children;
    for (auto it = fs::directory_iterator(current, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code statusEc;
        if (!it->is_directory(statusEc)) continue;

        auto childPath = it->path();
        children.push_back({displayNameForPath(childPath), childPath.string(), false});
    }

    if (ec) {
        pickerError_ = "Could not read " + pickerCurrentPath_ + ": " + ec.message();
    }

    std::sort(children.begin(), children.end(), [](const PickEntry& a, const PickEntry& b) {
        return a.name < b.name;
    });

    pickerEntries_.insert(pickerEntries_.end(), children.begin(), children.end());
    pickerCursor_ = std::clamp(pickerCursor_, 0, std::max(0, (int)pickerEntries_.size() - 1));
}

void App::openPickerEntry() {
    if (pickerEntries_.empty()) return;
    pickerCurrentPath_ = pickerEntries_[pickerCursor_].path;
    pickerCursor_ = 0;
    refreshPicker();
}

void App::scanPickerEntry() {
    if (pickerEntries_.empty()) {
        if (!pickerCurrentPath_.empty()) beginScan(pickerCurrentPath_);
        return;
    }
    beginScan(pickerEntries_[pickerCursor_].path);
}

void App::goToPickerParent() {
#ifdef PLATFORM_WINDOWS
    if (pickerCurrentPath_.empty()) return;
#endif

    namespace fs = std::filesystem;
    fs::path current(pickerCurrentPath_);
    auto parent = current.parent_path();

#ifdef PLATFORM_WINDOWS
    if (parent.empty() || parent == current) {
        pickerCurrentPath_.clear();
    } else {
        pickerCurrentPath_ = parent.string();
    }
#else
    if (!parent.empty() && parent != current) {
        pickerCurrentPath_ = parent.string();
    }
#endif

    pickerCursor_ = 0;
    refreshPicker();
}

void App::beginScan(std::string path) {
    if (path.empty()) return;

    if (scanner_) scanner_->abort();

    rootPath_ = std::move(path);
    root_ = nullptr;
    treeView_.setRoot(nullptr);
    statusBar_.setSelected("", 0);
    {
        std::lock_guard<std::mutex> lock(progressMtx_);
        progress_ = ScanProgress{};
    }
    mode_ = ViewMode::Scanner;
    startScan();
}

Element App::renderPicker() {
    Elements rows;

    auto location = pickerCurrentPath_.empty() ? std::string("Computer") : pickerCurrentPath_;
    rows.push_back(
        hbox({
            text(" Target: ") | bold | color(Color::GrayLight),
            text(location) | color(Color::Cyan) | flex,
            text(" Enter scan  Right open  Left back  q quit ") | color(Color::GrayDark),
        }) | bgcolor(Color::RGB(20, 20, 40))
    );
    rows.push_back(separatorEmpty());

    if (!pickerError_.empty()) {
        rows.push_back(text(" " + pickerError_) | color(Color::Red));
        rows.push_back(separatorEmpty());
    }

    if (pickerEntries_.empty()) {
        rows.push_back(text(" No drives or folders found") | color(Color::GrayDark));
    } else {
        for (int i = 0; i < (int)pickerEntries_.size(); ++i) {
            const auto& entry = pickerEntries_[i];
            auto icon = entry.isParent ? std::string("↰ ") : std::string("▸ ");
            auto row = hbox({
                text(" "),
                text(icon) | color(entry.isParent ? Color::GrayLight : Color::Cyan),
                text(entry.name) | flex,
            });

            if (i == pickerCursor_) {
                row = row | bgcolor(Color::RGB(40, 50, 80)) | color(Color::White);
            }
            rows.push_back(row);
        }
    }

    return vbox(std::move(rows)) | flex;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────

int App::run() {
    if (rootPath_.empty()) {
        initializePicker();
    } else {
        startScan();
    }

    // Wire tree selection → status bar
    treeView_.onSelect = [this](FileNode* node) {
        statusBar_.setSelected(node->path, node->sizeBytes);
    };

    // Title bar
    auto titleBar = Renderer([] {
        return hbox({
            text("  💾 diskscangrok ") | bold | color(Color::Cyan),
            text("— WizTree-style disk visualizer (TUI)  ") | color(Color::GrayLight),
        }) | bgcolor(Color::RGB(10, 10, 30));
    });

    // Main layout: title / tree / status
    auto tree = treeView_.component();

    auto root = Renderer(tree, [&] {
        ScanProgress p;
        {
            std::lock_guard<std::mutex> lock(progressMtx_);
            p = progress_;
        }
        statusBar_.update(p);

        if (mode_ == ViewMode::Picker) {
            return vbox({
                titleBar->Render(),
                separator(),
                renderPicker(),
            });
        }

        return vbox({
            titleBar->Render(),
            separator(),
            tree->Render() | flex,
            separator(),
            statusBar_.render(),
        });
    });

    // Global key handler
    auto withKeys = CatchEvent(root, [&](Event e) -> bool {
        if (mode_ == ViewMode::Picker) {
            if (e == Event::Character('q') || e == Event::Escape) {
                screen_.ExitLoopClosure()();
                return true;
            }
            if (e == Event::ArrowUp || e == Event::Character('k')) {
                if (pickerCursor_ > 0) --pickerCursor_;
                return true;
            }
            if (e == Event::ArrowDown || e == Event::Character('j')) {
                if (pickerCursor_ < (int)pickerEntries_.size() - 1) ++pickerCursor_;
                return true;
            }
            if (e == Event::Home) {
                pickerCursor_ = 0;
                return true;
            }
            if (e == Event::End) {
                pickerCursor_ = std::max(0, (int)pickerEntries_.size() - 1);
                return true;
            }
            if (e == Event::Return) {
                scanPickerEntry();
                return true;
            }
            if (e == Event::ArrowRight) {
                openPickerEntry();
                return true;
            }
            if (e == Event::ArrowLeft || e == Event::Backspace) {
                goToPickerParent();
                return true;
            }
            return false;
        }

        // 'q' / Escape → quit
        if (e == Event::Character('q') || e == Event::Escape) {
            screen_.ExitLoopClosure()();
            return true;
        }
        // 'r' → rescan
        if (e == Event::Character('r')) {
            if (scanner_) scanner_->abort();
            root_ = nullptr;
            treeView_.setRoot(nullptr);
            startScan();
            return true;
        }
        return false;
    });

    screen_.Loop(withKeys);
    return 0;
}

} // namespace diskscan

#include "App.hpp"
#include "search/Search.hpp"
#include "StatusBar.hpp"
#include "util/FileOperations.hpp"

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

void App::startScan(bool forceRescan) {
    scanner_ = std::make_unique<Scanner>(rootPath_, forceRescan);
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
            rebuildIndex();
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
    fileIndex_.clear();
    searchView_.clear();
    searchQuery_.clear();
    searchSummary_.clear();
    searchFocused_ = false;
    contentTab_ = 0;
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

void App::rebuildIndex() {
    fileIndex_.build(root_);
    refreshSearch();
}

void App::refreshSearch() {
    if (searchQuery_.empty()) {
        searchView_.clear();
        searchSummary_.clear();
        contentTab_ = 0;
        return;
    }

    auto results = Search::query(fileIndex_, searchQuery_);
    searchSummary_ = std::to_string(results.size());
    if (results.size() >= 5000) {
        searchSummary_ += "+";
    }
    searchView_.setResults(std::move(results), fileIndex_.totalBytes());
    contentTab_ = 1;
}

void App::leaveSearchInput() {
    searchFocused_ = false;
    if (contentTab_ == 1) {
        searchView_.notifySelection();
    }
}

bool App::isSearchNavigationEvent(const Event& e) const {
    return e == Event::ArrowDown || e == Event::ArrowUp ||
           e == Event::Character('j') || e == Event::Character('k') ||
           e == Event::PageDown || e == Event::PageUp ||
           e == Event::Home || e == Event::End;
}

bool App::routeSearchResultsEvent(const Event& e) {
    if (contentTab_ != 1 || searchQuery_.empty()) {
        return false;
    }
    return searchView_.handleEvent(e);
}

const IndexEntry* App::activeSearchEntry() const {
    if (contentTab_ != 1) return nullptr;
    return searchView_.selectedEntry();
}

void App::beginDeleteConfirm() {
    const auto* entry = activeSearchEntry();
    if (!entry) {
        statusBar_.setActionMessage("Select a search result first", true);
        return;
    }
    if (root_ && entry->path == root_->path) {
        statusBar_.setActionMessage("Cannot delete the scan root", true);
        return;
    }

    pendingPath_ = entry->path;
    confirmState_ = ConfirmState::Delete;
}

void App::beginDestinationPicker(FileAction action) {
    const auto* entry = activeSearchEntry();
    if (!entry) {
        statusBar_.setActionMessage("Select a search result first", true);
        return;
    }

    pendingAction_ = action;
    pendingPath_ = entry->path;
    confirmState_ = ConfirmState::None;
    mode_ = ViewMode::DestinationPicker;
    pickerCursor_ = 0;
#ifdef PLATFORM_WINDOWS
    pickerCurrentPath_.clear();
#else
    pickerCurrentPath_ = "/";
#endif
    refreshPicker();
}

void App::cancelPendingAction() {
    confirmState_ = ConfirmState::None;
    pendingAction_ = FileAction::None;
    pendingPath_.clear();
    if (mode_ == ViewMode::DestinationPicker) {
        mode_ = ViewMode::Scanner;
    }
}

void App::applyFileOpResult(const FileOpResult& result, const std::string& affectedPath,
                            bool removeFromScan) {
    statusBar_.setActionMessage(result.message, !result.success);
    if (!result.success) return;

    if (removeFromScan && root_) {
        removePathFromTree(root_, affectedPath);
        treeView_.refresh();
        rebuildIndex();
    } else {
        rebuildIndex();
    }
}

void App::confirmDelete() {
    const std::string path = pendingPath_;
    cancelPendingAction();

    const auto result = deletePath(path);
    applyFileOpResult(result, path, true);
}

void App::confirmDestination() {
    std::string destination;
    if (!pickerCurrentPath_.empty()) {
        destination = pickerCurrentPath_;
    } else if (!pickerEntries_.empty()) {
        destination = pickerEntries_[pickerCursor_].path;
    }

    if (destination.empty()) {
        statusBar_.setActionMessage("Choose a destination folder", true);
        return;
    }

    const auto action = pendingAction_;
    const std::string source = pendingPath_;
    cancelPendingAction();

    FileOpResult result;
    bool removeFromScan = false;
    if (action == FileAction::Copy) {
        result = copyPath(source, destination);
    } else if (action == FileAction::Move) {
        result = movePath(source, destination);
        removeFromScan = true;
    } else {
        return;
    }

    applyFileOpResult(result, source, removeFromScan);
}

Element App::renderConfirmBanner() const {
    if (confirmState_ != ConfirmState::Delete || pendingPath_.empty()) {
        return text("");
    }

    return vbox({
        separatorEmpty(),
        hbox({
            text(" Delete ") | bold | color(Color::Red),
            text(pendingPath_) | color(Color::White) | flex,
            text("  y confirm  n cancel ") | color(Color::GrayDark),
        }) | bgcolor(Color::RGB(60, 20, 20)),
        separatorEmpty(),
    });
}

Element App::renderDestinationPicker() {
    Elements rows;
    const char* actionLabel = pendingAction_ == FileAction::Move ? "Move to" : "Copy to";

    auto location = pickerCurrentPath_.empty() ? std::string("Computer") : pickerCurrentPath_;
    rows.push_back(
        hbox({
            text(" ") | color(Color::GrayLight),
            text(actionLabel) | bold | color(Color::Cyan),
            text("  ") | color(Color::GrayLight),
            text(pendingPath_) | color(Color::Yellow),
        }) | bgcolor(Color::RGB(20, 20, 40))
    );
    rows.push_back(
        hbox({
            text(" Destination: ") | bold | color(Color::GrayLight),
            text(location) | color(Color::Cyan) | flex,
            text(" Enter confirm  Right open  Left back  Esc cancel ") | color(Color::GrayDark),
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

Element App::renderSearchBar() const {
    auto prompt = text(" / Search: ") | bold | color(Color::Cyan);
    auto query = searchQuery_.empty()
        ? text("type to filter (audio: video: pictures: documents: compressed: executable: folders:)") |
              color(Color::GrayDark)
        : text(searchQuery_) | color(Color::White);
    const bool resultsActive = !searchQuery_.empty() && !searchFocused_;
    auto cursor = searchFocused_ ? text("_") | blink : text("");
    auto mode = resultsActive
        ? text("  [RESULTS] ") | bold | color(Color::Green)
        : (searchFocused_ ? text("  [TYPING] ") | bold | color(Color::Yellow) : text(""));

    Elements right;
    if (searchFocused_) {
        right.push_back(text("  Enter or ↓/jk to browse results  Esc cancel  ") | color(Color::GrayDark));
    } else if (!searchSummary_.empty()) {
        right.push_back(text(searchSummary_ + " matches") | color(Color::Yellow));
        right.push_back(text("  ↑↓/jk nav  / edit  d del  c copy  m move  Esc clear  ") |
                        color(Color::GrayDark));
    }

    return hbox({
        prompt,
        query,
        cursor,
        mode,
        hbox(std::move(right)) | flex | align_right,
    }) | bgcolor(searchFocused_ ? Color::RGB(35, 30, 55) : Color::RGB(25, 25, 45));
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
    searchView_.onSelect = [this](const IndexEntry* entry) {
        if (entry) {
            statusBar_.setSelected(entry->path, entry->sizeBytes);
        }
    };
    searchView_.onNavigate = [this]() {
        searchFocused_ = false;
    };

    // Title bar
    auto titleBar = Renderer([] {
        return hbox({
            text("  💾 diskscangrok ") | bold | color(Color::Cyan),
            text("— WizTree-style disk visualizer (TUI)  ") | color(Color::GrayLight),
        }) | bgcolor(Color::RGB(10, 10, 30));
    });

    // Main layout: title / tree or search / status
    auto tree = treeView_.component();
    auto search = searchView_.component();
    auto scannerPanel = Container::Tab({tree, search}, &contentTab_);

    auto root = Renderer(scannerPanel, [&] {
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

        if (mode_ == ViewMode::DestinationPicker) {
            return vbox({
                titleBar->Render(),
                separator(),
                renderDestinationPicker(),
                separator(),
                statusBar_.render(),
            });
        }

        return vbox({
            titleBar->Render(),
            separator(),
            renderSearchBar(),
            renderConfirmBanner(),
            separatorEmpty(),
            scannerPanel->Render() | flex,
            separator(),
            statusBar_.render(),
        });
    });

    // Global key handler
    auto withKeys = CatchEvent(root, [&](Event e) -> bool {
        if (confirmState_ == ConfirmState::Delete) {
            if (e == Event::Character('y') || e == Event::Return) {
                confirmDelete();
                return true;
            }
            if (e == Event::Character('n') || e == Event::Escape) {
                cancelPendingAction();
                return true;
            }
            return true;
        }

        if (mode_ == ViewMode::DestinationPicker) {
            if (e == Event::Escape) {
                cancelPendingAction();
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
                confirmDestination();
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

        if (contentTab_ == 1 && !searchFocused_ && confirmState_ == ConfirmState::None) {
            if (e == Event::Character('d')) {
                beginDeleteConfirm();
                return true;
            }
            if (e == Event::Character('c')) {
                beginDestinationPicker(FileAction::Copy);
                return true;
            }
            if (e == Event::Character('m')) {
                beginDestinationPicker(FileAction::Move);
                return true;
            }
        }

        if (e == Event::Character('/')) {
            searchFocused_ = true;
            if (!searchQuery_.empty()) {
                contentTab_ = 1;
            }
            return true;
        }

        if (contentTab_ == 1 && !searchQuery_.empty() && isSearchNavigationEvent(e)) {
            searchFocused_ = false;
            routeSearchResultsEvent(e);
            return true;
        }

        if (searchFocused_) {
            if (e == Event::Return || e == Event::Tab || e == Event::Character('\t')) {
                leaveSearchInput();
                return true;
            }
            if (e == Event::Escape) {
                searchQuery_.clear();
                searchFocused_ = false;
                refreshSearch();
                return true;
            }
            if (e == Event::Backspace) {
                if (!searchQuery_.empty()) {
                    searchQuery_.pop_back();
                    refreshSearch();
                }
                return true;
            }
            if (e == Event::Character(' ')) {
                searchQuery_.push_back(' ');
                refreshSearch();
                return true;
            }
            if (e.is_character()) {
                const std::string ch = e.character();
                if (!ch.empty() && ch[0] != '/') {
                    searchQuery_ += ch;
                    refreshSearch();
                }
                return true;
            }
        } else if (e == Event::Escape && !searchQuery_.empty()) {
            searchQuery_.clear();
            refreshSearch();
            return true;
        }

        // 'q' / Escape → quit
        if (e == Event::Character('q') || (e == Event::Escape && searchQuery_.empty())) {
            screen_.ExitLoopClosure()();
            return true;
        }
        // 'r' → rescan (bypasses cached MFT and refreshes the local database)
        if (e == Event::Character('r')) {
            if (scanner_) scanner_->abort();
            root_ = nullptr;
            treeView_.setRoot(nullptr);
            fileIndex_.clear();
            searchView_.clear();
            searchQuery_.clear();
            searchSummary_.clear();
            searchFocused_ = false;
            contentTab_ = 0;
            startScan(true);
            return true;
        }
        return false;
    });

    screen_.Loop(withKeys);
    return 0;
}

} // namespace diskscan

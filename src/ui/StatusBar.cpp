#include "StatusBar.hpp"
#include "scanner/Scanner.hpp"

#include <ftxui/dom/elements.hpp>

#include <sstream>
#include <iomanip>

using namespace ftxui;

namespace diskscan {

// ─────────────────────────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────────────────────────

std::string formatSize(uint64_t bytes) {
    constexpr uint64_t KiB = 1024ULL;
    constexpr uint64_t MiB = 1024 * KiB;
    constexpr uint64_t GiB = 1024 * MiB;
    constexpr uint64_t TiB = 1024 * GiB;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (bytes >= TiB)      ss << (double)bytes / TiB << " TiB";
    else if (bytes >= GiB) ss << (double)bytes / GiB << " GiB";
    else if (bytes >= MiB) ss << (double)bytes / MiB << " MiB";
    else if (bytes >= KiB) ss << (double)bytes / KiB << " KiB";
    else                   ss << bytes                 << " B";

    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// StatusBar
// ─────────────────────────────────────────────────────────────────────────────

void StatusBar::update(const ScanProgress& p) {
    done_ = p.done;
    error_ = !p.errorMessage.empty();
    if (!p.errorMessage.empty()) {
        statusText_ = "Scan failed: " + p.errorMessage;
    } else if (!p.done) {
        statusText_ = "Scanning";
        if (!p.scanMethod.empty()) {
            statusText_ += " [" + p.scanMethod + "]";
        }
        statusText_ += "... " + std::to_string(p.filesFound) + " items  " +
                      formatSize(p.bytesAccounted) + "  " + p.currentPath.substr(
                          0, std::min((size_t)50, p.currentPath.size()));
        if (!p.warningMessage.empty()) {
            statusText_ += "  " + p.warningMessage;
        }
    } else {
        statusText_ = "Done";
        if (!p.scanMethod.empty()) {
            statusText_ += " [" + p.scanMethod + "]";
        }
        statusText_ += " - " + std::to_string(p.filesFound) + " items";
        if (!p.warningMessage.empty()) {
            statusText_ += "  " + p.warningMessage;
        }
    }
}

void StatusBar::setSelected(const std::string& path, uint64_t bytes) {
    selectedPath_  = path;
    selectedBytes_ = bytes;
}

Element StatusBar::render() const {
    auto keyHints = hbox({
        text(" ↑↓") | bold,
        text(" nav "),
        text("Enter") | bold,
        text(" expand "),
        text("←→") | bold,
        text(" collapse/expand "),
        text("r") | bold,
        text(" rescan "),
        text("q") | bold,
        text(" quit "),
    }) | color(Color::GrayDark);

    Element selInfo;
    if (!selectedPath_.empty()) {
        selInfo = hbox({
            text(" ▶ ") | color(Color::Cyan),
            text(selectedPath_) | color(Color::White),
            text("  "),
            text(formatSize(selectedBytes_)) | color(Color::Yellow) | bold,
        });
    } else {
        selInfo = text("");
    }

    auto statusColor = error_ ? Color::Red : (done_ ? Color::Green : Color::Yellow);
    auto scanStatus = text(" " + statusText_ + " ") | color(statusColor);

    return hbox({
        scanStatus,
        separator(),
        selInfo | flex,
        separator(),
        keyHints,
    }) | bgcolor(Color::RGB(30, 30, 30));
}

} // namespace diskscan

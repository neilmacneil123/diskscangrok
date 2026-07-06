#include "SearchView.hpp"
#include "StatusBar.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <string>

using namespace ftxui;

namespace diskscan {

namespace {

std::string parentPath(const std::string& path) {
    const auto slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash);
}

} // namespace

SearchView::SearchView() {
    comp_ = Renderer([this] { return render(0); }) | CatchEvent([this](Event e) -> bool {
        if (results_.empty()) return false;

        const int before = cursor_;
        if (e == Event::ArrowUp || e == Event::Character('k')) {
            if (cursor_ > 0) { --cursor_; }
        } else if (e == Event::ArrowDown || e == Event::Character('j')) {
            if (cursor_ < (int)results_.size() - 1) { ++cursor_; }
        } else if (e == Event::PageUp) {
            cursor_ = std::max(0, cursor_ - 20);
        } else if (e == Event::PageDown) {
            cursor_ = std::min((int)results_.size() - 1, cursor_ + 20);
        } else if (e == Event::Home) {
            cursor_ = 0;
        } else if (e == Event::End) {
            cursor_ = (int)results_.size() - 1;
        } else {
            return false;
        }

        if (cursor_ != before) {
            if (onNavigate) {
                onNavigate();
            }
            notifySelection();
        }
        return true;
    });
}

void SearchView::setResults(std::vector<const IndexEntry*> results, uint64_t totalIndexedBytes) {
    results_ = std::move(results);
    totalBytes_ = totalIndexedBytes;
    cursor_ = 0;
    notifySelection();
}

const IndexEntry* SearchView::selectedEntry() const {
    if (results_.empty() || cursor_ < 0 || cursor_ >= (int)results_.size()) {
        return nullptr;
    }
    return results_[cursor_];
}

void SearchView::notifySelection() {
    if (onSelect) {
        onSelect(selectedEntry());
    }
}

void SearchView::clear() {
    results_.clear();
    totalBytes_ = 0;
    cursor_ = 0;
}

ftxui::Component SearchView::component() { return comp_; }

Element SearchView::renderRow(const IndexEntry* entry, bool selected) {
    auto kind = entry->type == NodeType::Directory ? text("▸ ") | color(Color::Cyan)
                                                   : text("  ") | color(Color::GrayDark);
    auto name = text(entry->name) | bold | color(entry->type == NodeType::Directory
                                                     ? Color::Cyan
                                                     : Color::White);
    auto path = text("  " + parentPath(entry->path)) | color(Color::GrayDark);
    auto size = text(" " + formatSize(entry->sizeBytes) + " ") | color(Color::Yellow);

    auto row = hbox({
        kind,
        name,
        path | flex,
        size,
    });

    if (selected) {
        row = row | bgcolor(Color::RGB(40, 50, 80)) | color(Color::White);
    }
    return row;
}

Element SearchView::render(int /*availableHeight*/) {
    if (results_.empty()) {
        return vbox({
            text("") | flex,
            text("  No matches — refine your query") | color(Color::GrayDark),
            text("  Type a query above, press Enter, then use ↑↓ to browse results") |
                color(Color::GrayDark),
            text("") | flex,
        }) | flex;
    }

    auto header = hbox({
        text(" Match") | bold | flex,
        text(" Size ") | color(Color::GrayLight) | bold,
    }) | bgcolor(Color::RGB(20, 20, 40));

    constexpr int WINDOW = 50;
    int start = std::max(0, cursor_ - WINDOW / 2);
    int end = std::min((int)results_.size(), start + WINDOW);
    if (end - start < WINDOW && start > 0) {
        start = std::max(0, end - WINDOW);
    }

    Elements rows;
    rows.push_back(header);
    rows.push_back(separatorEmpty());

    for (int i = start; i < end; ++i) {
        rows.push_back(renderRow(results_[i], i == cursor_));
    }

    if ((int)results_.size() > WINDOW) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "  match %d / %zu", cursor_ + 1, results_.size());
        rows.push_back(separatorEmpty());
        rows.push_back(text(buf) | color(Color::GrayDark) | align_right);
    }

    return vbox(std::move(rows)) | flex;
}

} // namespace diskscan
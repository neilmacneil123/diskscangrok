#include "TreeView.hpp"
#include "StatusBar.hpp" // for formatSize

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <string>

using namespace ftxui;

namespace diskscan {

namespace {

// ── Bar chart cell ───────────────────────────────────────────────────────────
// Renders a [████░░░░░░] bar proportional to size.
Element sizeBar(uint64_t nodeSize, uint64_t rootSize, int barWidth = 20) {
    if (rootSize == 0 || barWidth <= 0) return text(std::string(barWidth, ' '));

    double ratio = std::min(1.0, (double)nodeSize / (double)rootSize);
    int    filled = (int)(ratio * barWidth);

    std::string bar;
    bar.reserve(barWidth);
    for (int i = 0; i < filled;    ++i) bar += "█";
    for (int i = filled; i < barWidth; ++i) bar += "░";

    // Colour gradient: green → yellow → red
    Color col = Color::Green;
    if (ratio > 0.5) col = Color::Yellow;
    if (ratio > 0.8) col = Color::Red;

    return text(bar) | color(col);
}

// ── Size percentage text ─────────────────────────────────────────────────────
std::string pctStr(uint64_t nodeSize, uint64_t rootSize) {
    if (rootSize == 0) return "  0.0%";
    double p = 100.0 * (double)nodeSize / (double)rootSize;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%5.1f%%", p);
    return buf;
}

// ── Icon ─────────────────────────────────────────────────────────────────────
std::string icon(const FileNode* n) {
    if (!n->isDir()) return "  ";
    return n->expanded ? "▼ " : "▶ ";
}

// ── Indent ───────────────────────────────────────────────────────────────────
std::string indent(int depth) {
    std::string s;
    for (int i = 0; i < depth; ++i) s += "  ";
    return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// TreeView
// ─────────────────────────────────────────────────────────────────────────────

TreeView::TreeView() {
    comp_ = Renderer([this] { return render(0); }) | CatchEvent([this](Event e) -> bool {
        if (flat_.empty()) return false;

        if (e == Event::ArrowUp || e == Event::Character('k')) {
            if (cursor_ > 0) { --cursor_; return true; }
        }
        if (e == Event::ArrowDown || e == Event::Character('j')) {
            if (cursor_ < (int)flat_.size() - 1) { ++cursor_; return true; }
        }
        if (e == Event::PageUp) {
            cursor_ = std::max(0, cursor_ - 20);
            return true;
        }
        if (e == Event::PageDown) {
            cursor_ = std::min((int)flat_.size() - 1, cursor_ + 20);
            return true;
        }
        if (e == Event::Home) {
            cursor_ = 0;
            return true;
        }
        if (e == Event::End) {
            cursor_ = (int)flat_.size() - 1;
            return true;
        }

        // Enter or Right: expand directory / navigate
        if (e == Event::Return || e == Event::ArrowRight) {
            auto* n = flat_[cursor_];
            if (n->isDir()) {
                n->expanded = !n->expanded;
                refresh();
                return true;
            }
        }

        // Left: collapse (go to parent)
        if (e == Event::ArrowLeft) {
            auto* n = flat_[cursor_];
            if (n->isDir() && n->expanded) {
                n->expanded = false;
                refresh();
                return true;
            }
            if (n->parent) {
                // Move selection to parent
                for (int i = 0; i < (int)flat_.size(); ++i) {
                    if (flat_[i] == n->parent) { cursor_ = i; break; }
                }
                if (n->parent->expanded) {
                    n->parent->expanded = false;
                    refresh();
                }
                return true;
            }
        }

        // Notify selection changes
        if (onSelect) onSelect(flat_[cursor_]);
        return false;
    });
}

void TreeView::setRoot(std::shared_ptr<FileNode> root) {
    root_   = std::move(root);
    cursor_ = 0;
    scroll_ = 0;
    if (root_) root_->expanded = true;
    refresh();
}

void TreeView::refresh() {
    flat_.clear();
    if (root_) root_->flatten(flat_);
    cursor_ = std::clamp(cursor_, 0, std::max(0, (int)flat_.size() - 1));
}

ftxui::Component TreeView::component() { return comp_; }

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

Element TreeView::renderRow(FileNode* node, bool selected, uint64_t rootSize) {
    auto indentStr  = indent(node->depth);
    auto iconStr    = icon(node);
    auto nameStr    = indentStr + iconStr + node->name;

    auto nameElem   = text(nameStr);
    if (node->isDir()) nameElem = nameElem | bold | color(Color::Cyan);
    else               nameElem = nameElem | color(Color::White);

    auto sizeStr    = formatSize(node->sizeBytes);
    auto sizeElem   = text(" " + sizeStr + " ") | color(Color::Yellow);

    auto pctElem    = text(pctStr(node->sizeBytes, rootSize) + " ")
                    | color(Color::GrayLight);

    auto bar        = sizeBar(node->sizeBytes, rootSize, 18);

    auto row = hbox({
        nameElem | flex,
        pctElem,
        bar,
        text(" "),
        sizeElem,
    });

    if (selected) {
        row = row | bgcolor(Color::RGB(40, 50, 80)) | color(Color::White);
    }

    return row;
}

Element TreeView::render(int /*availableHeight*/) {
    if (flat_.empty()) {
        return vbox({
            text("") | flex,
            text("  No data — press r to scan") | color(Color::GrayDark),
            text("") | flex,
        }) | flex;
    }

    uint64_t rootSize = root_ ? root_->sizeBytes : 1;

    // Header row
    auto header = hbox({
        text(" Name") | bold | flex,
        text("   %     Size bar           Size   ") | color(Color::GrayLight) | bold,
    }) | bgcolor(Color::RGB(20, 20, 40));

    // Determine visible window (simple scroll logic)
    // We'll use a flex list approach with ftxui's vbox; for very large lists
    // we only render a window of ±50 rows around the cursor.
    constexpr int WINDOW = 50;
    int start = std::max(0, cursor_ - WINDOW / 2);
    int end   = std::min((int)flat_.size(), start + WINDOW);
    // re-adjust so end is always WINDOW items if possible
    if (end - start < WINDOW && start > 0)
        start = std::max(0, end - WINDOW);

    Elements rows;
    rows.push_back(header);
    rows.push_back(separatorEmpty());

    for (int i = start; i < end; ++i) {
        rows.push_back(renderRow(flat_[i], i == cursor_, rootSize));
    }

    // Scroll indicator
    if ((int)flat_.size() > WINDOW) {
        char   buf[48];
        std::snprintf(buf, sizeof(buf), "  row %d / %zu", cursor_ + 1, flat_.size());
        rows.push_back(separatorEmpty());
        rows.push_back(
            text(buf) | color(Color::GrayDark) | align_right
        );
    }

    return vbox(std::move(rows)) | flex;
}

} // namespace diskscan

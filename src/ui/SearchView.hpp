#pragma once

#include "search/FileIndex.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <vector>

namespace diskscan {

/// Flat, scrollable search-results list sorted by size.
class SearchView {
public:
    SearchView();

    void setResults(std::vector<const IndexEntry*> results, uint64_t totalIndexedBytes);
    void clear();

    ftxui::Component component();

    const IndexEntry* selectedEntry() const;
    void notifySelection();
    bool handleEvent(const ftxui::Event& event);

    std::function<void(const IndexEntry*)> onSelect;
    std::function<void()> onNavigate;

private:
    ftxui::Element render(int availableHeight);
    ftxui::Element renderRow(const IndexEntry* entry, bool selected);

    std::vector<const IndexEntry*> results_;
    uint64_t                       totalBytes_ = 0;
    int                            cursor_ = 0;

    ftxui::Component comp_;
};

} // namespace diskscan
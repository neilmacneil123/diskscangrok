#pragma once

#include "model/FileNode.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <vector>
#include <functional>

namespace diskscan {

/// Scrollable, keyboard-navigated file tree with inline size bars.
class TreeView {
public:
    TreeView();

    void setRoot(std::shared_ptr<FileNode> root);
    void refresh();   // re-flatten from current tree state

    ftxui::Component component();

    std::function<void(FileNode*)> onSelect; // called on Enter

private:
    ftxui::Element render(int availableHeight);
    ftxui::Element renderRow(FileNode* node, bool selected, uint64_t rootSize);

    std::shared_ptr<FileNode>  root_;
    std::vector<FileNode*>     flat_;   // flattened visible nodes
    int                        cursor_ = 0;
    int                        scroll_  = 0;

    ftxui::Component           comp_;
};

} // namespace diskscan

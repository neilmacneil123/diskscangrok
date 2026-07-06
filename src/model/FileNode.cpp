#include "FileNode.hpp"

#include <algorithm>

namespace diskscan {

void FileNode::computeSizes() {
    if (type == NodeType::File) return;
    sizeBytes = 0;
    for (auto& child : children) {
        child->computeSizes();
        sizeBytes += child->sizeBytes;
    }
}

void FileNode::sortChildren() {
    std::sort(children.begin(), children.end(),
              [](const std::shared_ptr<FileNode>& a,
                 const std::shared_ptr<FileNode>& b) {
                  return a->sizeBytes > b->sizeBytes;
              });
    for (auto& child : children) {
        child->sortChildren();
    }
}

void FileNode::flatten(std::vector<FileNode*>& out) {
    out.push_back(this);
    if (isDir() && expanded) {
        for (auto& child : children) {
            child->flatten(out);
        }
    }
}

} // namespace diskscan

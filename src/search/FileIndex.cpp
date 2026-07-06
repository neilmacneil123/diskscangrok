#include "FileIndex.hpp"

#include <algorithm>
#include <cctype>

namespace diskscan {
namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string extensionOf(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return {};
    return toLower(name.substr(dot + 1));
}

} // namespace

void FileIndex::clear() {
    entries_.clear();
    totalBytes_ = 0;
}

void FileIndex::build(const std::shared_ptr<FileNode>& root) {
    clear();
    if (!root) return;

    totalBytes_ = root->sizeBytes;
    addNode(*root);

    std::vector<FileNode*> pending;
    for (auto& child : root->children) {
        pending.push_back(child.get());
    }

    while (!pending.empty()) {
        FileNode* node = pending.back();
        pending.pop_back();
        addNode(*node);
        for (auto& child : node->children) {
            pending.push_back(child.get());
        }
    }
}

void FileIndex::addNode(const FileNode& node) {
    IndexEntry entry;
    entry.path = node.path;
    entry.name = node.name;
    entry.pathLower = toLower(node.path);
    entry.nameLower = toLower(node.name);
    entry.extensionLower = extensionOf(node.name);
    entry.sizeBytes = node.sizeBytes;
    entry.type = node.type;
    entry.node = const_cast<FileNode*>(&node);
    entries_.push_back(std::move(entry));
}

} // namespace diskscan
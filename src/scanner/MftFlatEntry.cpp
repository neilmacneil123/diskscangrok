#ifdef PLATFORM_WINDOWS

#include "MftFlatEntry.hpp"

#include <sstream>

namespace diskscan {

std::shared_ptr<FileNode> buildMftTree(
    const std::vector<MftFlatEntry>& entries,
    const std::string& volumePath,
    std::string* errorMessage) {

    if (entries.empty()) {
        if (errorMessage) {
            *errorMessage = "MFT cache contained no entries";
        }
        return nullptr;
    }

    std::unordered_map<uint64_t, MftFlatEntry> entryMap;
    entryMap.reserve(entries.size());
    for (const auto& entry : entries) {
        entryMap[entry.mftId] = entry;
    }

    if (entryMap.find(5) == entryMap.end()) {
        MftFlatEntry rootEntry;
        rootEntry.mftId = 5;
        rootEntry.name = volumePath + "\\";
        rootEntry.isDir = true;
        entryMap[5] = rootEntry;
    }

    std::unordered_map<uint64_t, std::shared_ptr<FileNode>> nodes;
    nodes.reserve(entryMap.size());

    for (const auto& [id, fe] : entryMap) {
        auto node = std::make_shared<FileNode>();
        node->name = fe.name;
        node->sizeBytes = fe.allocSize;
        node->type = fe.isDir ? NodeType::Directory : NodeType::File;
        nodes[id] = node;
    }

    std::shared_ptr<FileNode> root;
    for (const auto& [id, fe] : entryMap) {
        if (!nodes.count(id)) continue;

        auto& child = nodes[id];
        const uint64_t parentId = fe.parentMftId & 0x0000FFFFFFFFFFFFULL;
        if (parentId == id || !nodes.count(parentId)) {
            if (id == 5) {
                root = child;
                root->name = volumePath + "\\";
            }
            continue;
        }

        auto& parent = nodes[parentId];
        child->parent = parent.get();
        child->depth = parent->depth + 1;
        parent->children.push_back(child);
    }

    if (!root) {
        for (auto& [id, node] : nodes) {
            if (!node->parent) {
                root = node;
                break;
            }
        }
    }

    if (!root) {
        if (errorMessage) {
            *errorMessage = "MFT tree build failed: no root record found";
        }
        return nullptr;
    }

    root->computeSizes();
    root->sortChildren();
    root->path = volumePath + "\\";

    std::function<void(FileNode*, const std::string&)> setPaths =
        [&](FileNode* node, const std::string& base) {
            for (auto& child : node->children) {
                child->path = base + child->name + (child->isDir() ? "\\" : "");
                setPaths(child.get(), child->path);
            }
        };
    setPaths(root.get(), root->path);

    return root;
}

} // namespace diskscan

#endif // PLATFORM_WINDOWS
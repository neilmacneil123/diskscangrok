#pragma once

#include "model/FileNode.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace diskscan {

/// Lightweight flat record for fast search over a scanned tree.
struct IndexEntry {
    std::string path;
    std::string name;
    std::string pathLower;
    std::string nameLower;
    std::string extensionLower;
    uint64_t    sizeBytes = 0;
    NodeType    type      = NodeType::File;
    FileNode*   node      = nullptr;
};

/// Flat index of every file and directory in a scan result.
class FileIndex {
public:
    void build(const std::shared_ptr<FileNode>& root);
    void clear();

    const std::vector<IndexEntry>& entries() const noexcept { return entries_; }
    uint64_t totalBytes() const noexcept { return totalBytes_; }

private:
    void addNode(const FileNode& node);

    std::vector<IndexEntry> entries_;
    uint64_t                totalBytes_ = 0;
};

} // namespace diskscan
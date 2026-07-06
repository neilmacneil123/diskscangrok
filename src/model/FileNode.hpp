#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diskscan {

enum class NodeType : uint8_t { File, Directory };

/// One entry in the filesystem tree.
struct FileNode {
    std::string              name;
    std::string              path;          // full absolute path
    uint64_t                 sizeBytes = 0; // allocated on disk (preferred)
    NodeType                 type      = NodeType::File;
    int                      depth     = 0;
    bool                     expanded  = false;

    std::vector<std::shared_ptr<FileNode>> children;
    FileNode*                              parent = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────────
    bool isDir() const noexcept { return type == NodeType::Directory; }

    /// Recursively sum children sizes into this node.
    void computeSizes();

    /// Sort children by descending size (largest first).
    void sortChildren();

    /// Flat list of visible nodes (respecting expanded state) for rendering.
    void flatten(std::vector<FileNode*>& out);
};

} // namespace diskscan

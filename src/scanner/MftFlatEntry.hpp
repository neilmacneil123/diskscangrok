#pragma once

#ifdef PLATFORM_WINDOWS

#include "model/FileNode.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diskscan {

/// One row from the NTFS MFT, used for caching and tree construction.
struct MftFlatEntry {
    uint64_t    mftId       = 0;
    std::string name;
    uint64_t    parentMftId = 0;
    uint64_t    allocSize   = 0;
    bool        isDir       = false;
};

/// Reconstruct a FileNode tree from flat MFT rows (record 5 = volume root).
std::shared_ptr<FileNode> buildMftTree(
    const std::vector<MftFlatEntry>& entries,
    const std::string& volumePath,
    std::string* errorMessage = nullptr);

} // namespace diskscan

#endif // PLATFORM_WINDOWS
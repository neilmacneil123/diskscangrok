#pragma once

#include "model/FileNode.hpp"

#include <memory>
#include <string>

namespace diskscan {

struct FileOpResult {
    bool        success = false;
    std::string message;
};

/// Delete a file or directory from disk.
FileOpResult deletePath(const std::string& path);

/// Copy a file or directory into destDirectory.
FileOpResult copyPath(const std::string& sourcePath, const std::string& destDirectory);

/// Move a file or directory into destDirectory.
FileOpResult movePath(const std::string& sourcePath, const std::string& destDirectory);

/// Remove a path from an in-memory scan tree and recompute parent sizes.
bool removePathFromTree(const std::shared_ptr<FileNode>& root, const std::string& path);

} // namespace diskscan
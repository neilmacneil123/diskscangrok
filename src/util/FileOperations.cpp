#include "FileOperations.hpp"

#include <algorithm>
#include <filesystem>

#ifdef PLATFORM_WINDOWS
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace diskscan {
namespace {

namespace fs = std::filesystem;

fs::path pathForFs(const std::string& path) {
#ifdef PLATFORM_WINDOWS
    if (path.size() >= 4 && path.rfind("\\\\?\\", 0) == 0) {
        return fs::path(path);
    }
    if (path.size() >= 2 && path[1] == ':') {
        std::wstring wide = L"\\\\?\\";
        wide.append(path.begin(), path.end());
        return fs::path(wide);
    }
#endif
    return fs::path(path);
}

FileOpResult fail(std::string message) {
    return {false, std::move(message)};
}

FileOpResult success(std::string message) {
    return {true, std::move(message)};
}

FileNode* findParentOfPath(FileNode* node, const std::string& targetPath) {
    if (!node) return nullptr;
    for (auto& child : node->children) {
        if (child->path == targetPath) {
            return node;
        }
        if (auto* parent = findParentOfPath(child.get(), targetPath)) {
            return parent;
        }
    }
    return nullptr;
}

void recomputeUp(FileNode* node) {
    while (node) {
        node->computeSizes();
        node = node->parent;
    }
}

} // namespace

FileOpResult deletePath(const std::string& path) {
    if (path.empty()) {
        return fail("No path selected");
    }

    std::error_code ec;
    const auto fsPath = pathForFs(path);
    if (!fs::exists(fsPath, ec)) {
        return fail("Path does not exist: " + path);
    }

    if (fs::is_directory(fsPath, ec) && !ec) {
        if (!fs::remove_all(fsPath, ec) || ec) {
            return fail("Could not delete folder: " + ec.message());
        }
        return success("Deleted folder " + path);
    }

    if (!fs::remove(fsPath, ec) || ec) {
        return fail("Could not delete file: " + ec.message());
    }
    return success("Deleted " + path);
}

FileOpResult copyPath(const std::string& sourcePath, const std::string& destDirectory) {
    if (sourcePath.empty() || destDirectory.empty()) {
        return fail("Source and destination are required");
    }

    std::error_code ec;
    const auto source = pathForFs(sourcePath);
    const auto destDir = pathForFs(destDirectory);

    if (!fs::exists(source, ec)) {
        return fail("Source does not exist: " + sourcePath);
    }
    if (!fs::exists(destDir, ec) || !fs::is_directory(destDir, ec)) {
        return fail("Destination folder does not exist: " + destDirectory);
    }

    const auto target = destDir / source.filename();
    if (fs::exists(target, ec)) {
        return fail("Destination already exists: " + target.string());
    }

    if (fs::is_directory(source, ec) && !ec) {
        fs::copy(source, target, fs::copy_options::recursive, ec);
    } else {
        fs::copy(source, target, ec);
    }

    if (ec) {
        return fail("Copy failed: " + ec.message());
    }
    return success("Copied to " + target.string());
}

FileOpResult movePath(const std::string& sourcePath, const std::string& destDirectory) {
    if (sourcePath.empty() || destDirectory.empty()) {
        return fail("Source and destination are required");
    }

    std::error_code ec;
    const auto source = pathForFs(sourcePath);
    const auto destDir = pathForFs(destDirectory);

    if (!fs::exists(source, ec)) {
        return fail("Source does not exist: " + sourcePath);
    }
    if (!fs::exists(destDir, ec) || !fs::is_directory(destDir, ec)) {
        return fail("Destination folder does not exist: " + destDirectory);
    }

    const auto target = destDir / source.filename();
    if (fs::exists(target, ec)) {
        return fail("Destination already exists: " + target.string());
    }

    fs::rename(source, target, ec);
    if (ec) {
        const auto copyResult = copyPath(sourcePath, destDirectory);
        if (!copyResult.success) {
            return copyResult;
        }
        const auto deleteResult = deletePath(sourcePath);
        if (!deleteResult.success) {
            return fail("Copied but could not remove original: " + deleteResult.message);
        }
        return success("Moved to " + target.string());
    }

    return success("Moved to " + target.string());
}

bool removePathFromTree(const std::shared_ptr<FileNode>& root, const std::string& path) {
    if (!root || path.empty()) return false;
    if (root->path == path) {
        return false;
    }

    FileNode* parent = findParentOfPath(root.get(), path);
    if (!parent) return false;

    auto& children = parent->children;
    const auto it = std::remove_if(children.begin(), children.end(),
                                   [&](const std::shared_ptr<FileNode>& child) {
                                       return child->path == path;
                                   });
    if (it == children.end()) return false;

    children.erase(it, children.end());
    recomputeUp(parent);
    root->sortChildren();
    return true;
}

} // namespace diskscan
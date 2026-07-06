#include "Scanner.hpp"

#ifdef PLATFORM_WINDOWS
#  include <windows.h>
#  include "MftReader.hpp"
#  include "util/WindowsPath.hpp"
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <fts.h>
#  include <dirent.h>
#  include <cstring>
#endif

#include <stdexcept>
#include <unordered_map>
#include <filesystem>
#include <exception>
#include <cctype>
#include <vector>
#include <cstdlib>

namespace diskscan {

#ifdef PLATFORM_WINDOWS
namespace {

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        needed = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
        if (needed <= 0) return {};
        std::wstring out(static_cast<size_t>(needed - 1), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, out.data(), needed);
        return out;
    }
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), needed);
    return out;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int needed = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring ensureTrailingSlash(std::wstring path) {
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    return path;
}

// Support long paths on Windows
std::wstring makeLongPath(const std::wstring& p) {
    if (p.size() >= 4 && p.substr(0, 4) == L"\\\\?\\") return p;
    if (p.size() >= 2 && p[1] == L':') return L"\\\\?\\" + p;
    return p;
}

uint64_t allocatedSizeForPath(const std::filesystem::path& path) {
    auto wide = makeLongPath(path.wstring());
    DWORD high = 0;
    DWORD low  = GetCompressedFileSizeW(wide.c_str(), &high);
    if (low == INVALID_FILE_SIZE) {
        DWORD err = GetLastError();
        if (err != NO_ERROR) return 0;
    }
    return (static_cast<uint64_t>(high) << 32) | low;
}

uint64_t allocatedSizeForPath(const std::wstring& path) {
    auto wide = makeLongPath(path);
    DWORD high = 0;
    DWORD low  = GetCompressedFileSizeW(wide.c_str(), &high);
    if (low == INVALID_FILE_SIZE) {
        DWORD err = GetLastError();
        if (err != NO_ERROR) return 0;
    }
    return (static_cast<uint64_t>(high) << 32) | low;
}

uint64_t fileSizeFromFindData(const WIN32_FIND_DATAW& data) {
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
           static_cast<uint64_t>(data.nFileSizeLow);
}

void resetTreeDepths(FileNode* node, int depth) {
    if (!node) return;
    node->depth = depth;
    for (auto& child : node->children) {
        child->parent = node;
        resetTreeDepths(child.get(), depth + 1);
    }
}

std::shared_ptr<FileNode> findNodeByPath(const std::shared_ptr<FileNode>& node,
                                         const std::string& path) {
    if (!node) return nullptr;
    if (windowsPathsEqual(node->path, path)) return node;

    for (auto& child : node->children) {
        auto match = findNodeByPath(child, path);
        if (match) return match;
    }

    return nullptr;
}

void fillSizesFromFilesystem(FileNode* node) {
    if (!node) return;
    if (!node->isDir() && node->sizeBytes == 0 && !node->path.empty()) {
        // Fall back to Win32 API for accurate allocated size when MFT didn't provide it
        node->sizeBytes = allocatedSizeForPath(utf8ToWide(node->path));
    }
    for (auto& child : node->children) {
        fillSizesFromFilesystem(child.get());
    }
}

std::string rootDisplayName(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path fsPath(path);
    auto filename = fsPath.filename().string();
    return filename.empty() ? path : filename;
}

bool isWindowsDriveRoot(const std::string& path) {
    return path.size() == 3 &&
           std::isalpha(static_cast<unsigned char>(path[0])) &&
           path[1] == ':' &&
           path[2] == '\\';
}

bool mftReaderDisabled() {
    char value[8] = {};
    DWORD length = GetEnvironmentVariableA("DISKSCAN_USE_MFT", value, sizeof(value));
    // Default: try MFT (fast path) for drive roots when running as admin.
    // Set DISKSCAN_USE_MFT=0 to force the reliable (but slower) Win32 walker.
    if (length >= 1 && value[0] == '0') {
        return true;   // explicitly disabled
    }
    return false;      // default enabled (will still fail gracefully if not admin / non-NTFS)
}

uint64_t countNodes(const std::shared_ptr<FileNode>& root) {
    if (!root) return 0;
    uint64_t count = 1;
    for (const auto& child : root->children) {
        count += countNodes(child);
    }
    return count;
}

std::shared_ptr<FileNode> scanWithWin32DirectoryWalker(const std::string& scanRoot,
                                                       std::atomic<bool>& abort,
                                                       ScanProgress& prog,
                                                       const ProgressCb& cb) {
    struct PendingDir {
        std::shared_ptr<FileNode> node;
        std::wstring path;
    };

    auto root = std::make_shared<FileNode>();
    root->name = rootDisplayName(scanRoot);
    root->path = scanRoot;
    root->type = NodeType::Directory;
    root->expanded = true;

    std::vector<PendingDir> pending;
    pending.push_back({root, ensureTrailingSlash(utf8ToWide(scanRoot))});

    while (!pending.empty()) {
        if (abort) break;

        auto current = std::move(pending.back());
        pending.pop_back();

        std::wstring pattern = ensureTrailingSlash(current.path) + L"*";
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW(
            makeLongPath(pattern).c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);

        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }

        do {
            if (abort) break;
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
                continue;
            }

            const bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool isReparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            std::wstring childPathWide = ensureTrailingSlash(current.path) + data.cFileName;
            if (isDir) childPathWide = ensureTrailingSlash(childPathWide);

            auto node = std::make_shared<FileNode>();
            node->name = wideToUtf8(data.cFileName);
            node->path = wideToUtf8(childPathWide);
            node->type = isDir ? NodeType::Directory : NodeType::File;
            node->parent = current.node.get();
            node->depth = current.node->depth + 1;

            if (!isDir) {
                node->sizeBytes = allocatedSizeForPath(childPathWide);
                if (node->sizeBytes == 0) {
                    node->sizeBytes = fileSizeFromFindData(data);
                }
            }

            current.node->children.push_back(node);

            ++prog.filesFound;
            prog.bytesAccounted += node->sizeBytes;
            prog.currentPath = node->path;
            if (cb && (prog.filesFound % 500) == 0) cb(prog);

            if (isDir && !isReparse) {
                pending.push_back({node, childPathWide});
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    root->computeSizes();
    root->sortChildren();
    return root;
}

} // namespace
#endif

Scanner::Scanner(std::string rootPath)
    : rootPath_(std::move(rootPath)) {}

Scanner::~Scanner() {
    abort();
    if (worker_.joinable()) worker_.join();
}

void Scanner::start(ProgressCb cb) {
    if (running_) return;
    running_ = true;
    abort_   = false;
    worker_  = std::thread([this, cb = std::move(cb)]() mutable {
        try {
            workerFn(std::move(cb));
        } catch (const std::exception& ex) {
            running_ = false;
            ScanProgress prog;
            prog.done = true;
            prog.errorMessage = ex.what();
            if (cb) cb(prog);
        } catch (...) {
            running_ = false;
            ScanProgress prog;
            prog.done = true;
            prog.errorMessage = "Unknown scanner error";
            if (cb) cb(prog);
        }
    });
}

void Scanner::abort() {
    abort_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform scanner implementations
// ─────────────────────────────────────────────────────────────────────────────

#ifdef PLATFORM_WINDOWS

void Scanner::workerFn(ProgressCb cb) {
    ScanProgress prog;
    auto pathInfo = parseWindowsPath(rootPath_);
    const std::string scanRoot = pathInfo.hasDrive ? pathInfo.rootPath : rootPath_;
    const bool canTryMft = pathInfo.hasDrive && isWindowsDriveRoot(scanRoot);
    std::string mftError;

    MftReader::Config cfg;
    cfg.volumePath = pathInfo.volumePath;
    cfg.errorMessage = &mftError;
    cfg.abortFlag  = &abort_;
    cfg.progressCb = [&](uint64_t files) {
        prog.filesFound = files;
        prog.scanMethod = "MFT";
        prog.currentPath = "Reading NTFS master file table";
        if (cb) cb(prog);
    };

    if (canTryMft && !mftReaderDisabled()) {
        prog.scanMethod = "MFT";
        prog.currentPath = "Opening NTFS volume " + cfg.volumePath;
        if (cb) cb(prog);

        auto volumeRoot = MftReader::read(cfg);
        if (volumeRoot) {
            root_ = findNodeByPath(volumeRoot, scanRoot);
            if (root_) {
                root_->parent = nullptr;
                root_->expanded = true;
                resetTreeDepths(root_.get(), 0);
                // Fill zero sizes from filesystem (MFT size extraction can be incomplete)
                fillSizesFromFilesystem(root_.get());
                root_->computeSizes();
                root_->sortChildren();
                if (root_->children.empty()) {
                    mftError = "MFT read returned no entries under " + scanRoot;
                    root_.reset();
                }
            } else {
                mftError = "MFT tree did not contain requested root " + scanRoot;
            }
        }
    }

    // Fallback to filesystem traversal if MFT read fails (non-admin / non-NTFS / error)
    if (!root_) {
        if (canTryMft && !mftReaderDisabled()) {
            prog.warningMessage = "MFT unavailable (" +
                                  (mftError.empty() ? "no admin or error" : mftError) +
                                  "); using Win32 directory scan (slower but reliable)";
        } else if (canTryMft && mftReaderDisabled()) {
            prog.warningMessage = "MFT disabled by DISKSCAN_USE_MFT=0; using Win32 scan";
        } else if (pathInfo.hasDrive) {
            prog.warningMessage = "MFT only supported for exact drive roots; using Win32 directory scan";
        }
        prog.scanMethod = "Win32";
        prog.filesFound = 0;
        prog.bytesAccounted = 0;
        prog.currentPath = scanRoot;
        if (cb) cb(prog);
        goto stdfs_walk;
    }

    prog.done = true;
    prog.scanMethod = "MFT";
    prog.filesFound = countNodes(root_);
    prog.bytesAccounted = root_ ? root_->sizeBytes : 0;
    prog.currentPath = root_ ? root_->path : scanRoot;
    running_ = false;
    if (cb) cb(prog);
    return;

stdfs_walk:;
    // fall through to std::filesystem traversal (shared with POSIX below)
    {
#ifdef PLATFORM_WINDOWS
        root_ = scanWithWin32DirectoryWalker(scanRoot, abort_, prog, cb);
#else
        auto r = std::make_shared<FileNode>();
        r->name = rootDisplayName(scanRoot);
        r->path = scanRoot;
        r->type = NodeType::Directory;
        r->expanded = true;

        std::unordered_map<std::string, FileNode*> dirMap;
        dirMap[scanRoot] = r.get();

        namespace fs = std::filesystem;
        std::error_code ec;
        auto opts = fs::directory_options::skip_permission_denied;

        auto it = fs::recursive_directory_iterator(scanRoot, opts, ec);
        while (!ec && it != fs::recursive_directory_iterator()) {
            if (abort_) break;

            std::error_code entryEc;
            auto node     = std::make_shared<FileNode>();
            node->name    = it->path().filename().string();
            node->path    = it->path().string();
            node->type    = it->is_directory(entryEc) ? NodeType::Directory : NodeType::File;
            node->sizeBytes = node->isDir() ? 0 : allocatedSizeForPath(it->path());
            if (!node->isDir() && node->sizeBytes == 0) {
                node->sizeBytes = (uint64_t)it->file_size(entryEc);
            }

            std::string parentStr = it->path().parent_path().string();
            FileNode*   parent    = dirMap.count(parentStr) ? dirMap[parentStr] : r.get();
            node->parent = parent;
            node->depth  = parent->depth + 1;
            parent->children.push_back(node);

            if (node->isDir())
                dirMap[node->path] = node.get();

            ++prog.filesFound;
            prog.bytesAccounted += node->sizeBytes;
            prog.currentPath = node->path;
            if (cb && (prog.filesFound % 500) == 0) cb(prog);

            it.increment(ec);
        }

        r->computeSizes();
        r->sortChildren();
        root_ = r;
#endif
    }

    prog.done = true;
    running_  = false;
    if (cb) cb(prog);
}

#else // PLATFORM_POSIX

void Scanner::workerFn(ProgressCb cb) {
    ScanProgress prog;

    auto root      = std::make_shared<FileNode>();
    root->name     = rootPath_;
    root->path     = rootPath_;
    root->type     = NodeType::Directory;
    root->expanded = true;

    // fts_open for fast, no-follow traversal
    char* paths[] = { const_cast<char*>(rootPath_.c_str()), nullptr };
    FTS* fts = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR | FTS_XDEV, nullptr);
    if (!fts) {
        // Last resort: std::filesystem
        namespace fs = std::filesystem;
        std::unordered_map<std::string, FileNode*> dirMap;
        dirMap[rootPath_] = root.get();
        std::error_code ec;

        auto it = fs::recursive_directory_iterator(
            rootPath_, fs::directory_options::skip_permission_denied, ec);
        while (!ec && it != fs::recursive_directory_iterator()) {
            if (abort_) break;

            std::error_code entryEc;
            auto node     = std::make_shared<FileNode>();
            node->name    = it->path().filename().string();
            node->path    = it->path().string();
            node->type    = it->is_directory(entryEc) ? NodeType::Directory : NodeType::File;
            node->sizeBytes = node->isDir() ? 0 : (uint64_t)it->file_size(entryEc);

            std::string parentStr = it->path().parent_path().string();
            FileNode*   parent    = dirMap.count(parentStr) ? dirMap[parentStr] : root.get();
            node->parent = parent;
            node->depth  = parent->depth + 1;
            parent->children.push_back(node);
            if (node->isDir()) dirMap[node->path] = node.get();

            ++prog.filesFound;
            prog.bytesAccounted += node->sizeBytes;
            prog.currentPath = node->path;
            if (cb && (prog.filesFound % 1000) == 0) cb(prog);

            it.increment(ec);
        }
        root->computeSizes();
        root->sortChildren();
        root_ = root;
        prog.done = true;
        running_  = false;
        if (cb) cb(prog);
        return;
    }

    std::unordered_map<std::string, FileNode*> dirMap;
    dirMap[rootPath_] = root.get();

    FTSENT* entry;
    while ((entry = fts_read(fts)) != nullptr) {
        if (abort_) break;

        switch (entry->fts_info) {
            case FTS_D: { // directory — pre-order
                if (entry->fts_level == 0) break; // root itself
                auto node     = std::make_shared<FileNode>();
                node->name    = entry->fts_name;
                node->path    = entry->fts_path;
                node->type    = NodeType::Directory;
                node->sizeBytes = 0;

                std::string parentPath =
                    std::string(entry->fts_path,
                        entry->fts_pathlen - entry->fts_namelen - 1);
                if (parentPath.empty()) parentPath = rootPath_;

                FileNode* parent = dirMap.count(parentPath)
                                 ? dirMap[parentPath]
                                 : root.get();
                node->parent = parent;
                node->depth  = parent->depth + 1;
                parent->children.push_back(node);
                dirMap[node->path] = node.get();

                ++prog.filesFound;
                prog.currentPath = node->path;
                if (cb && (prog.filesFound % 2000) == 0) cb(prog);
                break;
            }
            case FTS_F:
            case FTS_SL: { // regular file or symlink
                auto node     = std::make_shared<FileNode>();
                node->name    = entry->fts_name;
                node->path    = entry->fts_path;
                node->type    = NodeType::File;
                // Use st_blocks * 512 for allocated-on-disk size (like WizTree)
                node->sizeBytes = (uint64_t)entry->fts_statp->st_blocks * 512ULL;

                std::string parentPath =
                    std::string(entry->fts_path,
                        entry->fts_pathlen - entry->fts_namelen - 1);
                if (parentPath.empty()) parentPath = rootPath_;

                FileNode* parent = dirMap.count(parentPath)
                                 ? dirMap[parentPath]
                                 : root.get();
                node->parent = parent;
                node->depth  = parent->depth + 1;
                parent->children.push_back(node);

                prog.bytesAccounted += node->sizeBytes;
                ++prog.filesFound;
                prog.currentPath = node->path;
                if (cb && (prog.filesFound % 2000) == 0) cb(prog);
                break;
            }
            default:
                break;
        }
    }
    fts_close(fts);

    root->computeSizes();
    root->sortChildren();
    root_ = root;

    prog.done = true;
    running_  = false;
    if (cb) cb(prog);
}

#endif // PLATFORM_POSIX

} // namespace diskscan

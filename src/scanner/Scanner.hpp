#pragma once

#include "model/FileNode.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace diskscan {

/// Progress info delivered to the UI during scanning.
struct ScanProgress {
    uint64_t    filesFound    = 0;
    uint64_t    bytesAccounted = 0;
    std::string currentPath;
    std::string scanMethod;
    std::string warningMessage;
    std::string errorMessage;
    bool        done = false;
};

using ProgressCb = std::function<void(const ScanProgress&)>;

/// Top-level scanner: prefers fast MFT on Windows drive roots (if enabled),
/// otherwise uses reliable Win32 / POSIX directory walking with allocated sizes.
class Scanner {
public:
    explicit Scanner(std::string rootPath);
    ~Scanner();

    /// Start an async scan.  `cb` is called from the worker thread.
    void start(ProgressCb cb);

    /// Request an abort.
    void abort();

    bool isRunning() const noexcept { return running_; }

    /// Returns the root node once scanning is complete (nullptr while running).
    std::shared_ptr<FileNode> result() const { return root_; }

private:
    void                      workerFn(ProgressCb cb);

    std::string               rootPath_;
    std::shared_ptr<FileNode> root_;
    std::thread               worker_;
    std::atomic<bool>         running_{false};
    std::atomic<bool>         abort_{false};
};

} // namespace diskscan

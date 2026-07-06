#pragma once

#include <string>

namespace diskscan {

struct WindowsPathInfo {
    std::string volumePath; // e.g. "C:"
    std::string rootPath;   // e.g. "C:\" or "C:\Users"
    bool        hasDrive = false;
};

WindowsPathInfo parseWindowsPath(std::string path);
std::string normalizeWindowsPathForCompare(std::string path);
bool windowsPathsEqual(const std::string& lhs, const std::string& rhs);

} // namespace diskscan

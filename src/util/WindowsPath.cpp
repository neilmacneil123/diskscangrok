#include "WindowsPath.hpp"

#include <algorithm>
#include <cctype>

namespace diskscan {
namespace {

bool isDriveLetter(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

void replaceSlashes(std::string& path) {
    std::replace(path.begin(), path.end(), '/', '\\');
}

bool isDriveRoot(const std::string& path) {
    return path.size() == 3 && isDriveLetter(path[0]) &&
           path[1] == ':' && path[2] == '\\';
}

} // namespace

WindowsPathInfo parseWindowsPath(std::string path) {
    replaceSlashes(path);

    WindowsPathInfo info;
    if (path.size() < 2 || !isDriveLetter(path[0]) || path[1] != ':') {
        info.rootPath = std::move(path);
        return info;
    }

    path[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));

    info.hasDrive = true;
    info.volumePath = path.substr(0, 2);

    if (path.size() == 2) {
        info.rootPath = info.volumePath + "\\";
        return info;
    }

    info.rootPath = std::move(path);
    return info;
}

std::string normalizeWindowsPathForCompare(std::string path) {
    path = parseWindowsPath(std::move(path)).rootPath;

    while (path.size() > 1 && path.back() == '\\' && !isDriveRoot(path)) {
        path.pop_back();
    }

    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return path;
}

bool windowsPathsEqual(const std::string& lhs, const std::string& rhs) {
    return normalizeWindowsPathForCompare(lhs) == normalizeWindowsPathForCompare(rhs);
}

} // namespace diskscan

#include "ui/App.hpp"
#include "util/WindowsPath.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string root;

    if (argc >= 2) {
        root = argv[1];
    }

#ifdef PLATFORM_WINDOWS
    auto pathInfo = diskscan::parseWindowsPath(root);
    if (!root.empty() && pathInfo.hasDrive) {
        root = pathInfo.rootPath;
    }
#endif

    if (!root.empty()) {
        // Validate path
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) {
            std::cerr << "Path does not exist: " << root << "\n";
            return 1;
        }
    }

    diskscan::App app(root);
    return app.run();
}

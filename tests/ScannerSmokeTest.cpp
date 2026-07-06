#include "scanner/Scanner.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    std::string scanPath = argc > 1 ? argv[1] : "tests";
    bool verbose = false;
    bool quickMft = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--verbose") verbose = true;
        if (std::string(argv[i]) == "--quick-mft") quickMft = true;
    }
    diskscan::Scanner scanner(scanPath);

    bool done = false;
    scanner.start([&](const diskscan::ScanProgress& progress) {
        if (verbose && (progress.done || progress.filesFound % 50000 == 0)) {
            std::cout << "progress"
                      << " method=" << progress.scanMethod
                      << " files=" << progress.filesFound
                      << " bytes=" << progress.bytesAccounted
                      << " path=\"" << progress.currentPath << "\"";
            if (!progress.warningMessage.empty()) {
                std::cout << " warning=\"" << progress.warningMessage << "\"";
            }
            if (!progress.errorMessage.empty()) {
                std::cout << " error=\"" << progress.errorMessage << "\"";
            }
            std::cout << "\n" << std::flush;
        }
        if (progress.done) done = true;
    });

    int maxWaitIterations = argc > 1 ? 24000 : 200;
    if (quickMft) maxWaitIterations = 40;  // very short probe for MFT startup
    for (int i = 0; i < maxWaitIterations && !done; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (quickMft && i > 10) break; // force early stop for probe
    }

    if (!done) {
        std::cerr << "scan did not finish for " << scanPath << "\n";
        return 1;
    }
    auto root = scanner.result();
    if (!root) {
        std::cerr << "scan returned no root for " << scanPath << "\n";
        return 1;
    }
    if (root->children.empty()) {
        std::cerr << "scan returned zero children for " << root->path << "\n";
        return 1;
    }

    if (argc > 1) {
        std::cout << root->path << "\n";
        for (const auto& child : root->children) {
            std::cout << child->name << "\n";
        }
    }

    return 0;
}

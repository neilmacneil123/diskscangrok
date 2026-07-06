#include "util/WindowsPath.hpp"

#include <cassert>

int main() {
    {
        auto path = diskscan::parseWindowsPath("C:");
        assert(path.hasDrive);
        assert(path.volumePath == "C:");
        assert(path.rootPath == "C:\\");
    }

    {
        auto path = diskscan::parseWindowsPath("C:\\");
        assert(path.hasDrive);
        assert(path.volumePath == "C:");
        assert(path.rootPath == "C:\\");
    }

    {
        auto path = diskscan::parseWindowsPath("C:\\directoryname");
        assert(path.hasDrive);
        assert(path.volumePath == "C:");
        assert(path.rootPath == "C:\\directoryname");
    }

    {
        auto path = diskscan::parseWindowsPath("C:/directoryname");
        assert(path.hasDrive);
        assert(path.volumePath == "C:");
        assert(path.rootPath == "C:\\directoryname");
    }

    {
        auto path = diskscan::parseWindowsPath("\\directoryname");
        assert(!path.hasDrive);
        assert(path.volumePath.empty());
        assert(path.rootPath == "\\directoryname");
    }

    assert(diskscan::windowsPathsEqual("C:\\directoryname\\", "c:/directoryname"));
    assert(diskscan::windowsPathsEqual("C:", "c:\\"));

    return 0;
}

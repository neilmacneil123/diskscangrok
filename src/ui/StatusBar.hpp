#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>
#include <cstdint>

namespace diskscan {

struct ScanProgress;

/// One-line bar at the bottom: scan status + selected item info + key hints.
class StatusBar {
public:
    void update(const ScanProgress& p);
    void setSelected(const std::string& path, uint64_t bytes);
    void setActionMessage(const std::string& message, bool isError = false);

    ftxui::Element render() const;

private:
    std::string  statusText_;
    std::string  selectedPath_;
    std::string  actionMessage_;
    uint64_t     selectedBytes_ = 0;
    bool         done_          = false;
    bool         error_         = false;
    bool         actionError_   = false;
};

/// Human-readable size string ("3.2 GiB", "512 KiB", etc.)
std::string formatSize(uint64_t bytes);

} // namespace diskscan

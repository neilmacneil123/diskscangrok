#pragma once

#include "scanner/Scanner.hpp"
#include "model/FileNode.hpp"
#include "ui/TreeView.hpp"
#include "ui/StatusBar.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace diskscan {

struct PickEntry {
    std::string name;
    std::string path;
    bool        isParent = false;
};

/// Top-level application state and loop.
class App {
public:
    explicit App(std::string rootPath);
    ~App();

    /// Enter the blocking TUI event loop.
    int run();

private:
    enum class ViewMode {
        Picker,
        Scanner,
    };

    void initializePicker();
    void refreshPicker();
    void openPickerEntry();
    void scanPickerEntry();
    void goToPickerParent();
    void beginScan(std::string path);

    void startScan();
    void onScanProgress(const ScanProgress& p);
    ftxui::Element renderPicker();

    std::string   rootPath_;
    ScanProgress  progress_;
    std::mutex    progressMtx_;
    ViewMode      mode_ = ViewMode::Scanner;

    std::string            pickerCurrentPath_;
    std::string            pickerError_;
    std::vector<PickEntry> pickerEntries_;
    int                    pickerCursor_ = 0;

    std::unique_ptr<Scanner>  scanner_;
    std::shared_ptr<FileNode> root_;

    ftxui::ScreenInteractive screen_;
    TreeView                  treeView_;
    StatusBar                 statusBar_;
};

} // namespace diskscan

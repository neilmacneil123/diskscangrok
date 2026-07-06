#pragma once

#include "scanner/Scanner.hpp"
#include "model/FileNode.hpp"
#include "search/FileIndex.hpp"
#include "util/FileOperations.hpp"
#include "ui/TreeView.hpp"
#include "ui/SearchView.hpp"
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
        DestinationPicker,
    };

    enum class FileAction {
        None,
        Copy,
        Move,
    };

    enum class ConfirmState {
        None,
        Delete,
    };

    void initializePicker();
    void refreshPicker();
    void openPickerEntry();
    void scanPickerEntry();
    void goToPickerParent();
    void beginScan(std::string path);

    void startScan();
    void onScanProgress(const ScanProgress& p);
    void rebuildIndex();
    void refreshSearch();
    void beginDeleteConfirm();
    void beginDestinationPicker(FileAction action);
    void cancelPendingAction();
    void confirmDelete();
    void confirmDestination();
    void applyFileOpResult(const FileOpResult& result, const std::string& affectedPath,
                           bool removeFromScan);
    const IndexEntry* activeSearchEntry() const;

    ftxui::Element renderPicker();
    ftxui::Element renderDestinationPicker();
    ftxui::Element renderConfirmBanner() const;
    ftxui::Element renderSearchBar() const;

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
    FileIndex                 fileIndex_;

    std::string searchQuery_;
    std::string searchSummary_;
    bool        searchFocused_ = false;
    int         contentTab_    = 0; // 0 = tree, 1 = search results

    ConfirmState confirmState_     = ConfirmState::None;
    FileAction   pendingAction_    = FileAction::None;
    std::string  pendingPath_;

    ftxui::ScreenInteractive screen_;
    TreeView                  treeView_;
    SearchView                searchView_;
    StatusBar                 statusBar_;
};

} // namespace diskscan

#include "explorer_folder_ops.hpp"

#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <cctype>
#include <string>
#include <system_error>

namespace tundraux::explorer {

void beginCreateFolder(ExplorerState& state) {
    state.creatingFolder = true;
    state.newFolderName.clear();
    state.message = "Enter new folder name";
}

void createFolderFromInput(ExplorerState& state) {
    const std::string folderName = trimCopy(state.newFolderName);
    if (!isValidFolderName(folderName)) {
        state.message = redMessage("Invalid folder name.");
        return;
    }

    const fs::path target = state.currentPath / fs::u8path(folderName);
    if (!isPathInsideRoot(target, state.rootPath)) {
        state.message = redMessage("Cannot create outside explorer root.");
        return;
    }

    std::error_code error;
    if (fs::exists(target, error)) {
        state.message = redMessage("Folder already exists: " + folderName);
        return;
    }

    fs::create_directory(target, error);
    if (error) {
        state.message = redMessage("Create folder failed: " + error.message());
        return;
    }

    state.creatingFolder = false;
    state.newFolderName.clear();
    refresh(state);
    selectPath(state, target);
    state.message = "Created folder " + folderName;
}

void handleCreateFolderInput(ExplorerState& state, const KeyPress& key) {
    switch (key.key) {
        case Key::Escape:
            state.creatingFolder = false;
            state.newFolderName.clear();
            state.message = "Create folder cancelled";
            break;
        case Key::Enter:
            createFolderFromInput(state);
            break;
        case Key::Backspace:
            if (!state.newFolderName.empty()) {
                state.newFolderName.pop_back();
            }
            break;
        case Key::Character:
            if (std::isprint(static_cast<unsigned char>(key.character)) &&
                state.newFolderName.size() < 120) {
                state.newFolderName.push_back(key.character);
            }
            break;
        default:
            break;
    }
}

}

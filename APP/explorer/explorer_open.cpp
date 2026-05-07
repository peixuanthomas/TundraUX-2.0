#include "explorer_open.hpp"

#include "TUXfile.hpp"
#include "editor.hpp"
#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <windows.h>
#include <shellapi.h>

namespace tundraux::explorer {

void openSelected(ExplorerState& state) {
    const FileEntry* entry = selectedEntry(state);
    if (entry == nullptr) {
        state.message = "Nothing selected";
        return;
    }

    const FileEntry selected = *entry;
    if (selected.isDirectory) {
        if (!isPathInsideRoot(selected.path, state.rootPath)) {
            state.message = "Cannot leave explorer root";
            return;
        }
        state.currentPath = selected.path;
        state.cursor = 0;
        state.scroll = 0;
        refresh(state);
        return;
    }

    if (extensionOf(selected.path) == ".tux") {
        std::cout << "\x1b[?25h" << std::flush;
        const int result = open_tux_file_in_editor(
            pathToDisplayString(selected.path),
            selected.name,
            state.username,
            state.usertype,
            true
        );
        std::cout << "\x1b[?25l" << std::flush;
        if (result == 0) {
            state.message = "Decrypted and edited " + selected.name;
            refresh(state);
        } else if (result == 2) {
            state.message = redMessage("TUX file is corrupted or invalid.");
        } else if (result == 3) {
            state.message = redMessage("Access denied: only the creator, admin, or debug can edit this TUX file.");
        } else if (result == 7) {
            state.message = "Viewed read-only " + selected.name;
        } else {
            state.message = redMessage("Failed to decrypt and open TUX file.");
        }
        return;
    }
    if (extensionOf(selected.path) == ".dat") {
        state.message = redMessage("User data file cannot be opened from explorer.");
        return;
    }

    if (opensWithEditor(selected.path)) {
        std::cout << "\x1b[?25h" << std::flush;
        const int result = run_editor(pathToDisplayString(selected.path), selected.name);
        std::cout << "\x1b[?25l" << std::flush;
        state.message = result == 0
            ? "Edited " + selected.name
            : "Editor exited with code " + std::to_string(result);
        refresh(state);
        return;
    }

    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        selected.path.wstring().c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        state.message = "Open failed";
    } else {
        state.message = "Opened " + selected.name;
    }
}

}

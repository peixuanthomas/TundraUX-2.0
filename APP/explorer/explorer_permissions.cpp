#include "explorer_permissions.hpp"

#include "TUXfile.hpp"
#include "explorer_directory.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

namespace tundraux::explorer {

std::string deletePermissionError(const ExplorerState& state, const FileEntry& entry) {
    const std::string extension = extensionOf(entry.path);
    if (!isDebugUser(state.usertype)) {
        if (!entry.isDirectory && extension == ".dat") {
            return "User data files are debug-only.";
        }
        if (entry.isDirectory && directoryContainsExtension(entry.path, ".dat")) {
            return "Folders containing user data files are debug-only.";
        }
    }

    if (!entry.isDirectory && extension == ".tux" &&
        !can_modify_tux_file(pathToDisplayString(entry.path), state.username, state.usertype)) {
        return "You can only modify TUX files you created.";
    }
    if (entry.isDirectory &&
        directory_has_protected_tux_files(pathToDisplayString(entry.path), state.username, state.usertype)) {
        return "Folder contains TUX files you cannot modify.";
    }

    return "";
}

std::string selectedTuxPermissionStatus(const ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        return "";
    }

    const FileEntry& entry = state.entries[state.cursor];
    if (entry.isDirectory || extensionOf(entry.path) != ".tux") {
        return "";
    }

    if (can_modify_tux_file(pathToDisplayString(entry.path), state.username, state.usertype)) {
        return "";
    }

    return redMessage("Access denied: current user cannot modify this TUX file.");
}

}

#include "explorer_delete.hpp"

#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_permissions.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <string>
#include <system_error>

namespace tundraux::explorer {

void requestDelete(ExplorerState& state) {
    const FileEntry* entry = selectedEntry(state);
    if (entry == nullptr) {
        state.message = "Nothing selected";
        return;
    }

    const std::string permissionError = deletePermissionError(state, *entry);
    if (!permissionError.empty()) {
        state.pendingDelete = false;
        state.message = redMessage(permissionError);
        return;
    }

    state.pendingDelete = true;
    state.pendingDeletePath = entry->path;
    state.pendingDeleteName = entry->name;
    state.message = redMessage("Press D to confirm delete: " + entry->name);
}

void confirmDelete(ExplorerState& state) {
    if (!state.pendingDelete) {
        state.message = "No pending delete";
        return;
    }

    const fs::path target = state.pendingDeletePath;
    if (!isPathInsideRoot(target, state.rootPath)) {
        state.pendingDelete = false;
        state.message = redMessage("Cannot delete outside explorer root.");
        return;
    }

    std::error_code error;
    if (!fs::exists(target, error)) {
        state.pendingDelete = false;
        refresh(state);
        state.message = "Delete skipped: item no longer exists";
        return;
    }

    FileEntry entry;
    entry.name = target.filename().u8string();
    entry.path = target;
    entry.isDirectory = fs::is_directory(target, error);
    if (error) {
        state.message = redMessage("Delete failed: " + error.message());
        return;
    }

    const std::string permissionError = deletePermissionError(state, entry);
    if (!permissionError.empty()) {
        state.pendingDelete = false;
        state.message = redMessage(permissionError);
        return;
    }

    if (entry.isDirectory) {
        fs::remove_all(target, error);
    } else {
        fs::remove(target, error);
    }

    if (error) {
        state.message = redMessage("Delete failed: " + error.message());
        return;
    }

    if (state.clipboard.mode != ClipboardMode::None &&
        (isSamePath(state.clipboard.path, target) ||
         (entry.isDirectory && isPathInsideRoot(state.clipboard.path, target)))) {
        state.clipboard = {};
    }

    const std::string deletedName = state.pendingDeleteName;
    state.pendingDelete = false;
    refresh(state);
    state.message = "Deleted " + deletedName;
}

}

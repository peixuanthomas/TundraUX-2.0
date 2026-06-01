#include "explorer_delete.hpp"

#include "audit_log.hpp"
#include "explorer_backend.hpp"
#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_permissions.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <string>
#include <system_error>

namespace tundraux::explorer {
namespace {

void setAuditUser(const ExplorerState& state) {
    tundraux::audit::setCurrentUser(USER{state.usertype, state.username, "", "", 0});
}

}

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
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "delete denied path=" + pathToDisplayString(entry->path) + " reason=" + permissionError
        );
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
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "delete denied path=" + pathToDisplayString(target) + " reason=outside root"
        );
        return;
    }

    if (state.backend != nullptr) {
        FileEntry entry;
        entry.name = target.filename().u8string();
        entry.path = target;
        entry.isDirectory = false;
        for (const auto& candidate : state.entries) {
            if (isSamePath(candidate.path, target)) {
                entry = candidate;
                break;
            }
        }

        const auto result = state.backend->deletePath(explorerRelativePath(state.rootPath, target), entry.isDirectory);
        if (!result.ok || !result.value) {
            state.message = redMessage(result.message);
            setAuditUser(state);
            tundraux::audit::logEvent(
                "explorer",
                "delete failure path=" + pathToDisplayString(target) + " reason=" + result.message
            );
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
        setAuditUser(state);
        tundraux::audit::logEvent("explorer", "delete success path=" + pathToDisplayString(target));
        return;
    }

    std::error_code error;
    if (!fs::exists(target, error)) {
        state.pendingDelete = false;
        refresh(state);
        state.message = "Delete skipped: item no longer exists";
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "delete failure path=" + pathToDisplayString(target) + " reason=missing"
        );
        return;
    }

    FileEntry entry;
    entry.name = target.filename().u8string();
    entry.path = target;
    entry.isDirectory = fs::is_directory(target, error);
    if (error) {
        state.message = redMessage("Delete failed: " + error.message());
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "delete failure path=" + pathToDisplayString(target) + " reason=" + error.message()
        );
        return;
    }

    const std::string permissionError = deletePermissionError(state, entry);
    if (!permissionError.empty()) {
        state.pendingDelete = false;
        state.message = redMessage(permissionError);
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "delete denied path=" + pathToDisplayString(target) + " reason=" + permissionError
        );
        return;
    }

    if (entry.isDirectory) {
        fs::remove_all(target, error);
    } else {
        fs::remove(target, error);
    }

    if (error) {
        state.message = redMessage("Delete failed: " + error.message());
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "delete failure path=" + pathToDisplayString(target) + " reason=" + error.message()
        );
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
    setAuditUser(state);
    tundraux::audit::logEvent("explorer", "delete success path=" + pathToDisplayString(target));
}

}

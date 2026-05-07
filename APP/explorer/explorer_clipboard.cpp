#include "explorer_clipboard.hpp"

#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_permissions.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <string>
#include <system_error>

namespace tundraux::explorer {
namespace {

bool copyClipboardItem(const ClipboardState& clipboard, const fs::path& target, std::error_code& error) {
    if (clipboard.isDirectory) {
        fs::copy(clipboard.path, target, fs::copy_options::recursive, error);
        return !error;
    }

    fs::copy_file(clipboard.path, target, error);
    return !error;
}

}

fs::path uniquePasteTarget(const fs::path& requestedTarget) {
    std::error_code error;
    if (!fs::exists(requestedTarget, error)) {
        return requestedTarget;
    }

    const fs::path parent = requestedTarget.parent_path();
    const std::string stem = requestedTarget.stem().u8string();
    const std::string extension = requestedTarget.extension().u8string();

    for (int copyIndex = 1; copyIndex < 1000; ++copyIndex) {
        std::string filename = stem + " - copy";
        if (copyIndex > 1) {
            filename += " " + std::to_string(copyIndex);
        }
        filename += extension;

        fs::path candidate = parent / fs::u8path(filename);
        if (!fs::exists(candidate, error)) {
            return candidate;
        }
    }

    return requestedTarget;
}

void markClipboard(ExplorerState& state, ClipboardMode mode) {
    const FileEntry* entry = selectedEntry(state);
    if (entry == nullptr) {
        state.message = "Nothing selected";
        return;
    }

    const std::string permissionError = deletePermissionError(state, *entry);
    if (!permissionError.empty()) {
        state.message = redMessage(permissionError);
        return;
    }

    state.clipboard.mode = mode;
    state.clipboard.path = entry->path;
    state.clipboard.name = entry->name;
    state.clipboard.isDirectory = entry->isDirectory;
    state.message = mode == ClipboardMode::Copy
        ? "Copied " + entry->name
        : "Cut " + entry->name;
}

void pasteClipboard(ExplorerState& state) {
    if (state.clipboard.mode == ClipboardMode::None) {
        state.message = "Clipboard is empty";
        return;
    }

    std::error_code error;
    if (!fs::exists(state.clipboard.path, error)) {
        state.message = redMessage("Clipboard source no longer exists.");
        state.clipboard = {};
        return;
    }

    FileEntry sourceEntry;
    sourceEntry.name = state.clipboard.name;
    sourceEntry.path = state.clipboard.path;
    sourceEntry.isDirectory = fs::is_directory(state.clipboard.path, error);
    if (error) {
        state.message = redMessage("Clipboard source check failed: " + error.message());
        return;
    }
    const std::string permissionError = deletePermissionError(state, sourceEntry);
    if (!permissionError.empty()) {
        state.message = redMessage(permissionError);
        return;
    }

    const fs::path requestedTarget = state.currentPath / fs::u8path(state.clipboard.name);
    if (state.clipboard.mode == ClipboardMode::Cut && isSamePath(state.clipboard.path, requestedTarget)) {
        state.clipboard = {};
        refresh(state);
        state.message = "Cut cancelled: item is already here";
        return;
    }

    fs::path target = uniquePasteTarget(requestedTarget);
    if (!isPathInsideRoot(target, state.rootPath)) {
        state.message = redMessage("Cannot paste outside explorer root.");
        return;
    }

    if (state.clipboard.isDirectory && isPathInsideRoot(target, state.clipboard.path)) {
        state.message = redMessage("Cannot paste a directory into itself.");
        return;
    }

    if (state.clipboard.mode == ClipboardMode::Copy) {
        copyClipboardItem(state.clipboard, target, error);
    } else {
        fs::rename(state.clipboard.path, target, error);
        if (error) {
            error.clear();
            if (copyClipboardItem(state.clipboard, target, error)) {
                if (state.clipboard.isDirectory) {
                    fs::remove_all(state.clipboard.path, error);
                } else {
                    fs::remove(state.clipboard.path, error);
                }
            }
        }
    }

    if (error) {
        state.message = redMessage("Paste failed: " + error.message());
        return;
    }

    const std::string pastedName = target.filename().u8string();
    state.clipboard = {};
    refresh(state);
    selectPath(state, target);
    state.message = "Pasted " + pastedName;
}

}

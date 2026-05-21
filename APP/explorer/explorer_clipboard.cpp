#include "explorer_clipboard.hpp"

#include "audit_log.hpp"
#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_permissions.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"

#include <string>
#include <system_error>
#include <windows.h>

namespace tundraux::explorer {
namespace {

void setAuditUser(const ExplorerState& state) {
    tundraux::audit::setCurrentUser(USER{state.usertype, state.username, "", "", 0});
}

bool copyClipboardItem(const ClipboardState& clipboard, const fs::path& target, std::error_code& error) {
    if (clipboard.isDirectory) {
        fs::copy(clipboard.path, target, fs::copy_options::recursive, error);
        return !error;
    }

    fs::copy_file(clipboard.path, target, error);
    return !error;
}

std::string win32Failure(const char* action) {
    return std::string(action) + " failed (" + std::to_string(GetLastError()) + ")";
}

bool writeUtf8TextToSystemClipboard(const std::string& text, std::string& error) {
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
    if (wideLength <= 0) {
        error = win32Failure("UTF-8 conversion");
        return false;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideLength) * sizeof(wchar_t));
    if (memory == nullptr) {
        error = win32Failure("GlobalAlloc");
        return false;
    }

    wchar_t* buffer = static_cast<wchar_t*>(GlobalLock(memory));
    if (buffer == nullptr) {
        error = win32Failure("GlobalLock");
        GlobalFree(memory);
        return false;
    }

    const int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, buffer, wideLength);
    GlobalUnlock(memory);
    if (converted <= 0) {
        error = win32Failure("UTF-8 conversion");
        GlobalFree(memory);
        return false;
    }

    if (!OpenClipboard(nullptr)) {
        error = win32Failure("OpenClipboard");
        GlobalFree(memory);
        return false;
    }

    if (!EmptyClipboard()) {
        error = win32Failure("EmptyClipboard");
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }

    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        error = win32Failure("SetClipboardData");
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }

    CloseClipboard();
    return true;
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
        setAuditUser(state);
        tundraux::audit::logEvent("explorer", "clipboard source failure mode=none reason=nothing selected");
        return;
    }

    const std::string permissionError = deletePermissionError(state, *entry);
    if (!permissionError.empty()) {
        state.message = redMessage(permissionError);
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "clipboard source denied mode=" +
                std::string(mode == ClipboardMode::Copy ? "copy" : "cut") +
                " path=" + pathToDisplayString(entry->path) + " reason=" + permissionError
        );
        return;
    }

    state.clipboard.mode = mode;
    state.clipboard.path = entry->path;
    state.clipboard.name = entry->name;
    state.clipboard.isDirectory = entry->isDirectory;
    setAuditUser(state);
    tundraux::audit::logEvent(
        "explorer",
        "clipboard source success mode=" +
            std::string(mode == ClipboardMode::Copy ? "copy" : "cut") +
            " path=" + pathToDisplayString(entry->path)
    );
    state.message = mode == ClipboardMode::Copy
        ? "Copied " + entry->name
        : "Cut " + entry->name;
}

void pasteClipboard(ExplorerState& state) {
    if (state.clipboard.mode == ClipboardMode::None) {
        state.message = "Clipboard is empty";
        setAuditUser(state);
        tundraux::audit::logEvent("explorer", "paste failure reason=clipboard empty");
        return;
    }

    const std::string sourcePath = pathToDisplayString(state.clipboard.path);
    const std::string mode = state.clipboard.mode == ClipboardMode::Copy ? "copy" : "cut";
    std::error_code error;
    if (!fs::exists(state.clipboard.path, error)) {
        state.message = redMessage("Clipboard source no longer exists.");
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "paste failure mode=" + mode + " source=" + sourcePath + " reason=source missing"
        );
        state.clipboard = {};
        return;
    }

    FileEntry sourceEntry;
    sourceEntry.name = state.clipboard.name;
    sourceEntry.path = state.clipboard.path;
    sourceEntry.isDirectory = fs::is_directory(state.clipboard.path, error);
    if (error) {
        state.message = redMessage("Clipboard source check failed: " + error.message());
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "paste failure mode=" + mode + " source=" + sourcePath + " reason=" + error.message()
        );
        return;
    }
    const std::string permissionError = deletePermissionError(state, sourceEntry);
    if (!permissionError.empty()) {
        state.message = redMessage(permissionError);
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "paste denied mode=" + mode + " source=" + sourcePath + " reason=" + permissionError
        );
        return;
    }

    const fs::path requestedTarget = state.currentPath / fs::u8path(state.clipboard.name);
    if (state.clipboard.mode == ClipboardMode::Cut && isSamePath(state.clipboard.path, requestedTarget)) {
        state.clipboard = {};
        refresh(state);
        state.message = "Cut cancelled: item is already here";
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "paste denied mode=" + mode + " source=" + sourcePath +
                " destination=" + pathToDisplayString(requestedTarget) + " reason=already in destination"
        );
        return;
    }

    fs::path target = uniquePasteTarget(requestedTarget);
    if (!isPathInsideRoot(target, state.rootPath)) {
        state.message = redMessage("Cannot paste outside explorer root.");
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "paste denied mode=" + mode + " source=" + sourcePath +
                " destination=" + pathToDisplayString(target) + " reason=outside root"
        );
        return;
    }

    if (state.clipboard.isDirectory && isPathInsideRoot(target, state.clipboard.path)) {
        state.message = redMessage("Cannot paste a directory into itself.");
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "paste denied mode=" + mode + " source=" + sourcePath +
                " destination=" + pathToDisplayString(target) + " reason=directory into itself"
        );
        return;
    }

    bool cutFallbackCreatedDestination = false;
    bool cutFallbackSourceRemovalFailed = false;
    std::string cutFallbackRemoveFailureReason;

    if (state.clipboard.mode == ClipboardMode::Copy) {
        copyClipboardItem(state.clipboard, target, error);
    } else {
        fs::rename(state.clipboard.path, target, error);
        if (error) {
            error.clear();
            if (copyClipboardItem(state.clipboard, target, error)) {
                cutFallbackCreatedDestination = true;
                if (state.clipboard.isDirectory) {
                    fs::remove_all(state.clipboard.path, error);
                } else {
                    fs::remove(state.clipboard.path, error);
                }
                if (error) {
                    cutFallbackSourceRemovalFailed = true;
                    cutFallbackRemoveFailureReason = error.message();
                }
            }
        }
    }

    if (error) {
        state.message = redMessage("Paste failed: " + error.message());
        setAuditUser(state);
        if (cutFallbackCreatedDestination && cutFallbackSourceRemovalFailed) {
            tundraux::audit::logEvent(
                "explorer",
                "paste partial mode=" + mode + " source=" + sourcePath +
                    " destination=" + pathToDisplayString(target) +
                    " mutation=destination created source_remove=failed reason=" +
                    cutFallbackRemoveFailureReason
            );
        }
        tundraux::audit::logEvent(
            "explorer",
            "paste failure mode=" + mode + " source=" + sourcePath +
                " destination=" + pathToDisplayString(target) + " reason=" + error.message()
        );
        return;
    }

    const std::string pastedName = target.filename().u8string();
    state.clipboard = {};
    refresh(state);
    selectPath(state, target);
    state.message = "Pasted " + pastedName;
    setAuditUser(state);
    tundraux::audit::logEvent(
        "explorer",
        "paste success mode=" + mode + " source=" + sourcePath +
            " destination=" + pathToDisplayString(target)
    );
}

bool copySelectedFileName(ExplorerState& state, SystemClipboardWriter writer) {
    const FileEntry* entry = selectedEntry(state);
    if (entry == nullptr) {
        state.message = "Nothing selected";
        setAuditUser(state);
        tundraux::audit::logEvent("explorer", "copy file name failure reason=nothing selected");
        return false;
    }

    std::string error;
    if (writer == nullptr || !writer(entry->name, error)) {
        if (error.empty()) {
            error = "clipboard writer unavailable";
        }
        state.message = redMessage("Could not copy file name: " + error);
        setAuditUser(state);
        tundraux::audit::logEvent(
            "explorer",
            "copy file name failure path=" + pathToDisplayString(entry->path) + " reason=" + error
        );
        return false;
    }

    state.message = "Copied file name: " + entry->name;
    setAuditUser(state);
    tundraux::audit::logEvent(
        "explorer",
        "copy file name success path=" + pathToDisplayString(entry->path)
    );
    return true;
}

void copySelectedFileNameToSystemClipboard(ExplorerState& state) {
    copySelectedFileName(state, writeUtf8TextToSystemClipboard);
}

}

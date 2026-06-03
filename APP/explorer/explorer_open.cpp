#include "explorer_open.hpp"

#include "editor.hpp"
#include "explorer_backend.hpp"
#include "explorer_directory.hpp"
#include "explorer_navigation.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"
#include "tux_editor.hpp"
#include "backend_facade.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <windows.h>
#include <shellapi.h>

namespace tundraux::audit {
int openTlogInEditor(
    const std::string&,
    const std::string&,
    const std::string&,
    const std::string&
);
}

namespace tundraux::explorer {
namespace {

void setAuditUser(const ExplorerState& state) {
    if (state.audit == nullptr) {
        return;
    }
    state.audit->setCurrentUser({state.usertype, state.username, "", 0});
}

void logAuditEvent(const ExplorerState& state, const std::string& category, const std::string& detail) {
    if (state.audit == nullptr) {
        return;
    }
    setAuditUser(state);
    state.audit->logEvent(category, detail);
}

class ScopedEditorTempFile {
public:
    ScopedEditorTempFile() = default;

    ~ScopedEditorTempFile() {
        cleanup();
    }

    ScopedEditorTempFile(const ScopedEditorTempFile&) = delete;
    ScopedEditorTempFile& operator=(const ScopedEditorTempFile&) = delete;

    bool create(const std::string& content, const std::string& extension = ".txt") {
        std::error_code error;
        std::filesystem::path tempRoot = std::filesystem::temp_directory_path(error);
        if (error) {
            return false;
        }

        tempRoot /= "TundraUX";
        std::filesystem::create_directories(tempRoot, error);
        if (error) {
            return false;
        }

        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const std::filesystem::path candidate =
                tempRoot / ("explorer-open-" + std::to_string(seed) + "-" + std::to_string(attempt) + extension);
            const DWORD createResult = writeNewFile(candidate, content);
            if (createResult == ERROR_FILE_EXISTS || createResult == ERROR_ALREADY_EXISTS) {
                continue;
            }
            if (createResult != ERROR_SUCCESS) {
                return false;
            }
            path_ = candidate;
            return true;
        }

        return false;
    }

    const std::filesystem::path& path() const {
        return path_;
    }

    void release() {
        path_.clear();
    }

private:
    static DWORD writeNewFile(const std::filesystem::path& path, const std::string& content) {
        HANDLE handle = CreateFileW(
            path.wstring().c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY,
            nullptr
        );
        if (handle == INVALID_HANDLE_VALUE) {
            return GetLastError();
        }

        DWORD errorCode = ERROR_SUCCESS;
        const char* cursor = content.data();
        std::size_t remaining = content.size();
        while (remaining > 0) {
            const DWORD chunk = remaining > static_cast<std::size_t>(MAXDWORD)
                ? MAXDWORD
                : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (!WriteFile(handle, cursor, chunk, &written, nullptr) || written == 0) {
                errorCode = GetLastError();
                break;
            }
            cursor += written;
            remaining -= written;
        }

        if (!CloseHandle(handle) && errorCode == ERROR_SUCCESS) {
            errorCode = GetLastError();
        }
        if (errorCode != ERROR_SUCCESS) {
            std::error_code removeError;
            std::filesystem::remove(path, removeError);
        }
        return errorCode;
    }

    void cleanup() {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove(path_, error);
            path_.clear();
        }
    }

    std::filesystem::path path_;
};

bool readTempFile(const std::filesystem::path& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return !file.bad();
}

std::string backendFailureMessage(const std::string& fallback, const ExplorerBackendResult<bool>& result) {
    return result.message.empty() ? fallback : result.message;
}

std::string backendFailureMessage(const std::string& fallback, const ExplorerBackendResult<std::string>& result) {
    return result.message.empty() ? fallback : result.message;
}

std::string tuxApiPathFromExplorerPath(const fs::path& rootPath, const fs::path& tuxPath) {
    std::string path = explorerRelativePath(rootPath, tuxPath);
    constexpr std::size_t extensionLength = 4;
    if (path.size() >= extensionLength &&
        toLowerCopy(path.substr(path.size() - extensionLength)) == ".tux") {
        path.resize(path.size() - extensionLength);
    }
    return path;
}

std::string auditApiPathFromExplorerPath(const fs::path& rootPath, const fs::path& tlogPath) {
    std::string path = explorerRelativePath(rootPath, tlogPath);
    constexpr const char* logsPrefix = "logs/";
    const std::string lowerPath = toLowerCopy(path);
    if (lowerPath.rfind(logsPrefix, 0) == 0) {
        path.erase(0, std::string(logsPrefix).size());
    }
    return path;
}

std::string joinLines(const std::vector<std::string>& lines) {
    std::string content;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        content += lines[i];
        if (i + 1 < lines.size()) {
            content += '\n';
        }
    }
    return content;
}

void openBackendTuxFile(ExplorerState& state, const FileEntry& selected) {
    const std::string backendPath = tuxApiPathFromExplorerPath(state.rootPath, selected.path);
    logAuditEvent(state, "explorer", "backend open " + backendPath);

    const auto readResult = state.backend->readTux(backendPath);
    if (!readResult.ok) {
        state.message = readResult.errorCode == "PermissionDenied"
            ? redMessage("Access denied: only the creator, admin, or debug can open this TUX file.")
            : redMessage(readResult.message.empty() ? "Failed to decrypt and open TUX file." : readResult.message);
        return;
    }

    ScopedEditorTempFile tempFile;
    if (!tempFile.create(readResult.value.content)) {
        state.message = redMessage("Unable to prepare editor file.");
        return;
    }

    std::cout << "\x1b[?25h" << std::flush;
    const int editorResult = run_editor(tempFile.path().string(), selected.name);
    std::cout << "\x1b[?25l" << std::flush;
    if (editorResult != 0) {
        state.message = "Editor exited with code " + std::to_string(editorResult);
        return;
    }

    std::string editedContent;
    if (!readTempFile(tempFile.path(), editedContent)) {
        state.message = redMessage("Unable to read editor output.");
        return;
    }

    if (editedContent != readResult.value.content) {
        const auto writeResult = state.backend->writeTux(backendPath, editedContent);
        if (!writeResult.ok || !writeResult.value) {
            state.message = redMessage(backendFailureMessage("Failed to save TUX file.", writeResult));
            return;
        }
    }

    state.message = "Decrypted and edited " + selected.name;
    refresh(state);
}

void openBackendTlogFile(ExplorerState& state, const FileEntry& selected) {
    const std::string backendPath = auditApiPathFromExplorerPath(state.rootPath, selected.path);
    logAuditEvent(state, "explorer", "backend open " + backendPath);

    const auto readResult = state.backend->readTlog(backendPath);
    if (!readResult.ok) {
        state.message = readResult.errorCode == "PermissionDenied"
            ? redMessage("Access denied: only admin or debug can open audit logs.")
            : redMessage(readResult.message.empty() ? "Failed to decrypt audit log." : readResult.message);
        return;
    }

    ScopedEditorTempFile tempFile;
    if (!tempFile.create(joinLines(readResult.value), ".log")) {
        state.message = redMessage("Unable to prepare audit log view.");
        return;
    }

    std::cout << "\x1b[?25h" << std::flush;
    const int editorResult = run_editor(tempFile.path().string(), selected.name);
    std::cout << "\x1b[?25l" << std::flush;
    state.message = editorResult == 0
        ? "Opened audit log " + selected.name
        : "Editor exited with code " + std::to_string(editorResult);
}

void openBackendPlainFile(ExplorerState& state, const FileEntry& selected) {
    const std::string backendPath = explorerRelativePath(state.rootPath, selected.path);
    logAuditEvent(state, "explorer", "backend open " + backendPath);

    const auto readResult = state.backend->readFile(backendPath);
    if (!readResult.ok) {
        state.message = readResult.errorCode == "PermissionDenied"
            ? redMessage("Access denied.")
            : redMessage(backendFailureMessage("Unable to open file.", readResult));
        return;
    }

    ScopedEditorTempFile tempFile;
    if (!tempFile.create(readResult.value)) {
        state.message = redMessage("Unable to prepare editor file.");
        return;
    }

    std::cout << "\x1b[?25h" << std::flush;
    const int editorResult = run_editor(tempFile.path().string(), selected.name);
    std::cout << "\x1b[?25l" << std::flush;
    if (editorResult != 0) {
        state.message = "Editor exited with code " + std::to_string(editorResult);
        return;
    }

    std::string editedContent;
    if (!readTempFile(tempFile.path(), editedContent)) {
        state.message = redMessage("Unable to read editor output.");
        return;
    }

    if (editedContent != readResult.value) {
        const auto writeResult = state.backend->writeFile(backendPath, editedContent);
        if (!writeResult.ok || !writeResult.value) {
            state.message = redMessage(backendFailureMessage("Failed to save file.", writeResult));
            return;
        }
    }

    state.message = "Edited " + selected.name;
    refresh(state);
}

void openBackendExternalFile(ExplorerState& state, const FileEntry& selected) {
    const std::string backendPath = explorerRelativePath(state.rootPath, selected.path);
    logAuditEvent(state, "explorer", "backend open " + backendPath);

    const auto readResult = state.backend->readFile(backendPath);
    if (!readResult.ok) {
        state.message = readResult.errorCode == "PermissionDenied"
            ? redMessage("Access denied.")
            : redMessage(backendFailureMessage("Unable to open file.", readResult));
        return;
    }

    std::string extension = extensionOf(selected.path);
    if (extension.empty()) {
        extension = ".tmp";
    }

    ScopedEditorTempFile tempFile;
    if (!tempFile.create(readResult.value, extension)) {
        state.message = redMessage("Unable to prepare file for opening.");
        return;
    }

    logAuditEvent(state, "explorer", "open temporary " + backendPath);
    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"open",
        tempFile.path().wstring().c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        state.message = "Open failed";
    } else {
        tempFile.release();
        state.message = "Opened " + selected.name;
    }
}

} // namespace

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
    const std::string selectedPath = pathToDisplayString(selected.path);

    if (extensionOf(selected.path) == ".tux") {
        if (state.backend != nullptr) {
            openBackendTuxFile(state, selected);
            return;
        }

        logAuditEvent(state, "explorer", "open " + selectedPath);
        std::cout << "\x1b[?25h" << std::flush;
        const int result = open_tux_file_in_editor(
            selectedPath,
            selected.name,
            state.username,
            state.usertype,
            true,
            state.audit
        );
        std::cout << "\x1b[?25l" << std::flush;
        if (result == 0) {
            state.message = "Decrypted and edited " + selected.name;
            refresh(state);
        } else if (result == 2) {
            state.message = redMessage("TUX file is corrupted or invalid.");
        } else if (result == 3) {
            state.message = redMessage("Access denied: only the creator, admin, or debug can open this TUX file.");
        } else if (result == 7) {
            state.message = "Viewed read-only " + selected.name;
        } else if (result == 8) {
            state.message = redMessage("Failed to save TUX file.");
        } else {
            state.message = redMessage("Failed to decrypt and open TUX file.");
        }
        return;
    }

    if (extensionOf(selected.path) == ".tlog") {
        if (state.backend != nullptr) {
            openBackendTlogFile(state, selected);
            return;
        }
        logAuditEvent(state, "explorer", "open " + selectedPath);
        std::cout << "\x1b[?25h" << std::flush;
        const int result = tundraux::audit::openTlogInEditor(
            selectedPath,
            selected.name,
            state.username,
            state.usertype
        );
        std::cout << "\x1b[?25l" << std::flush;
        if (result == 0) {
            state.message = "Opened audit log " + selected.name;
        } else if (result == 3) {
            state.message = redMessage("Access denied: only admin or debug can open audit logs.");
        } else {
            state.message = redMessage("Failed to decrypt audit log.");
        }
        return;
    }
    if (extensionOf(selected.path) == ".dat") {
        state.message = redMessage("User data file cannot be opened from explorer.");
        return;
    }

    if (opensWithEditor(selected.path)) {
        if (state.backend != nullptr) {
            openBackendPlainFile(state, selected);
            return;
        }

        logAuditEvent(state, "explorer", "open " + selectedPath);
        std::cout << "\x1b[?25h" << std::flush;
        const int result = run_editor(selectedPath, selected.name);
        std::cout << "\x1b[?25l" << std::flush;
        state.message = result == 0
            ? "Edited " + selected.name
            : "Editor exited with code " + std::to_string(result);
        refresh(state);
        return;
    }

    logAuditEvent(state, "explorer", "open " + selectedPath);
    if (state.backend != nullptr) {
        openBackendExternalFile(state, selected);
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

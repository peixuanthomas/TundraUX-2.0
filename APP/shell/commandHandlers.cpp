#include "commandHandlers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cwctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

#include "account_settings.hpp"
#include "audit_log.hpp"
#include "backend_runtime.hpp"
#include "build_info.hpp"
#include "color.hpp"
#include "editor.hpp"
#include "manageusers.hpp"
#include "explorer.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
namespace fs = std::filesystem;

fs::path normalizeExistingPath(const fs::path& path) {
    std::error_code error;
    fs::path normalized = fs::weakly_canonical(path, error);
    if (error) {
        normalized = fs::absolute(path, error);
    }
    if (error) {
        normalized = path;
    }
    return normalized.lexically_normal();
}

std::wstring normalizedPart(const fs::path& path) {
    std::wstring value = path.wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch == L'/') {
            return L'\\';
        }
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool isPathInsideRoot(const fs::path& candidate, const fs::path& root) {
    const fs::path candidatePath = normalizeExistingPath(candidate);
    const fs::path rootPath = normalizeExistingPath(root);
    auto candidateIt = candidatePath.begin();

    for (auto rootIt = rootPath.begin(); rootIt != rootPath.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidatePath.end()) {
            return false;
        }
        if (normalizedPart(*candidateIt) != normalizedPart(*rootIt)) {
            return false;
        }
    }

    return true;
}

bool hasUnsafePathPart(const fs::path& path) {
    for (const auto& part : path) {
        const std::string value = part.u8string();
        if (value == "." || value == "..") {
            return true;
        }
    }
    return false;
}

std::string readWholeFile(const fs::path& path, bool& ok) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        ok = false;
        return {};
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    ok = !stream.bad();
    return buffer.str();
}

bool writeWholeFile(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }

    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(stream);
}

bool writeNewFileAtomically(const fs::path& path, const std::string& content, DWORD& errorCode) {
    errorCode = ERROR_SUCCESS;
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
        errorCode = GetLastError();
        return false;
    }

    bool ok = true;
    const char* cursor = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const DWORD chunk = remaining > static_cast<std::size_t>(MAXDWORD)
            ? MAXDWORD
            : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(handle, cursor, chunk, &written, nullptr) || written == 0) {
            errorCode = GetLastError();
            ok = false;
            break;
        }
        cursor += written;
        remaining -= written;
    }

    if (!CloseHandle(handle)) {
        errorCode = GetLastError();
        ok = false;
    }

    if (!ok) {
        std::error_code error;
        fs::remove(path, error);
    }
    return ok;
}

class TemporaryEditorFile {
public:
    TemporaryEditorFile() = default;

    ~TemporaryEditorFile() {
        cleanup();
    }

    TemporaryEditorFile(const TemporaryEditorFile&) = delete;
    TemporaryEditorFile& operator=(const TemporaryEditorFile&) = delete;

    bool create(const std::string& content) {
        std::error_code error;
        fs::path tempRoot = fs::temp_directory_path(error);
        if (error) {
            return false;
        }

        tempRoot /= "TundraUX";
        fs::create_directories(tempRoot, error);
        if (error) {
            return false;
        }

        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const fs::path candidate =
                tempRoot / ("plain-edit-" + std::to_string(seed) + "-" + std::to_string(attempt) + ".tmp");
            DWORD createError = ERROR_SUCCESS;
            if (!writeNewFileAtomically(candidate, content, createError)) {
                if (createError == ERROR_FILE_EXISTS || createError == ERROR_ALREADY_EXISTS) {
                    continue;
                }
                return false;
            }
            path_ = candidate;
            return true;
        }

        return false;
    }

    const fs::path& path() const {
        return path_;
    }

private:
    void cleanup() {
        if (!path_.empty()) {
            std::error_code error;
            fs::remove(path_, error);
            path_.clear();
        }
    }

    fs::path path_;
};

std::string trimLeadingSpaces(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    return value;
}

bool usesBackend(tundraux::frontend::BackendRuntime* backendRuntime) {
    return backendRuntime != nullptr && !backendRuntime->legacyDirect();
}

USER guestUser() {
    return {
        "guest",
        "",
        "",
        "",
        0
    };
}

USER shellUserFromBackend(const tundraux::frontend::FrontendUser& user) {
    return {
        user.type,
        user.name,
        "",
        "",
        0
    };
}

std::string backendFailureMessage(
    const std::string& fallback,
    const std::string& errorCode,
    const std::string& backendMessage = ""
) {
    if (errorCode == "AuthenticationFailed") {
        return backendMessage.empty() ? "Login failed." : backendMessage;
    }
    if (errorCode == "StorageError") {
        return backendMessage.empty() ? fallback : backendMessage;
    }
    if (errorCode == "TransportError") {
        return "Backend unavailable.";
    }
    if (errorCode == "InvalidResponse") {
        return "Invalid backend response.";
    }
    if (errorCode == "SessionExpired") {
        return "Backend session expired.";
    }
    if (errorCode == "PermissionDenied") {
        return "Access Denied.";
    }
    return fallback;
}

bool ensureBackendSession(tundraux::frontend::BackendRuntime& backendRuntime) {
    if (!backendRuntime.sessionId().empty()) {
        return true;
    }

    auto* client = backendRuntime.client();
    if (client == nullptr) {
        colorcout("red", "Backend unavailable.\n");
        return false;
    }

    const auto guestSession = client->startGuestSession();
    if (!guestSession.ok) {
        colorcout("red", backendFailureMessage("Unable to start backend session.", guestSession.errorCode) + "\n");
        return false;
    }

    backendRuntime.setSessionId(guestSession.value.sessionId);
    return true;
}

bool refreshBackendGuestSession(tundraux::frontend::BackendRuntime& backendRuntime) {
    auto* client = backendRuntime.client();
    if (client == nullptr) {
        colorcout("red", "Backend unavailable.\n");
        return false;
    }

    const auto guestSession = client->startGuestSession();
    if (!guestSession.ok) {
        colorcout("red", backendFailureMessage("Unable to start backend guest session.", guestSession.errorCode) + "\n");
        return false;
    }

    backendRuntime.setSessionId(guestSession.value.sessionId);
    return true;
}

void displayLocalWhoami(const USER& currentUser) {
    if (currentUser.name.empty()) {
        colorcout("yellow", "No user is currently logged in.\n");
    } else {
        colorcout("white", "Current user: " + currentUser.name + " (" + currentUser.type + ")\n");
    }
}

void syncCurrentUserToGuest(USER& currentUser) {
    currentUser = guestUser();
    tundraux::audit::setCurrentUser(currentUser);
}

bool syncCurrentUserFromBackend(USER& currentUser, tundraux::frontend::BackendRuntime& backendRuntime) {
    auto* client = backendRuntime.client();
    if (client == nullptr || backendRuntime.sessionId().empty()) {
        syncCurrentUserToGuest(currentUser);
        return false;
    }

    const auto profile = client->currentProfile(backendRuntime.sessionId());
    if (profile.ok) {
        currentUser = shellUserFromBackend(profile.value);
        tundraux::audit::setCurrentUser(currentUser);
        return true;
    }

    syncCurrentUserToGuest(currentUser);
    return false;
}

void recoverBackendGuestSession(USER& currentUser, tundraux::frontend::BackendRuntime& backendRuntime) {
    backendRuntime.setSessionId("");
    if (refreshBackendGuestSession(backendRuntime)) {
        if (syncCurrentUserFromBackend(currentUser, backendRuntime)) {
            return;
        }
    }
    syncCurrentUserToGuest(currentUser);
}
}

void handleLoginCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    std::istringstream iss(input);
    std::string cmd;
    std::string username;
    std::string extra;
    iss >> cmd >> username >> extra;
    if (username.empty() || !extra.empty()) {
        colorcout("yellow", "Usage: login <username>\n");
        return;
    }

    if (usesBackend(backendRuntime)) {
        tundraux::audit::logEvent("login", "backend attempt");
        if (!ensureBackendSession(*backendRuntime)) {
            return;
        }

        const std::string password = getHiddenInput("Please enter password: ", '*');
        const auto result = backendRuntime->client()->login(backendRuntime->sessionId(), username, password);
        if (!result.ok) {
            tundraux::audit::logEvent("login", "backend failure " + result.errorCode);
            colorcout(
                "red",
                backendFailureMessage("Unable to complete backend login.", result.errorCode, result.message) + "\n"
            );
            return;
        }

        backendRuntime->setSessionId(result.value.sessionId);
        currentUser = shellUserFromBackend(result.value.user);
        tundraux::audit::setCurrentUser(currentUser);
        tundraux::audit::logEvent("login", "backend success");
        rollcout("green", "Welcome, " + currentUser.name + "!");
        return;
    }

    tundraux::audit::logEvent("login", "attempt " + username);
    DataManager dataManager("user_data.dat");
    const auto &users = dataManager.GetAllUsers();
    auto it = std::find_if(users.begin(), users.end(),
                           [&](const USER &u)
                           { return u.name == username; });

    if (it == users.end())
    {
        tundraux::audit::logEvent("login", "not-found " + username);
        colorcout("red", "User not found: " + username + "\n");
        return;
    }
    // disable user when count > 7
    if (it->count > 7)
    {
        tundraux::audit::logEvent("login", "locked " + username);
        colorcout("red", "User disabled due to too many failed attempts.\n");
        return;
    }
    std::string password = getHiddenInput("Please enter password for user " + username + ": ", '*');
    if (dataManager.ComparePassword(username, password))
    {
        USER updated = *it;
        updated.count = 0; // reset fail count on success
        dataManager.UpdateUser(username, updated);
        currentUser = updated;
        tundraux::audit::setCurrentUser(currentUser);
        tundraux::audit::logEvent("login", "success " + username);
        rollcout("green", "Welcome, " + currentUser.name + "!");
    }
    else
    {
        USER updated = *it;
        updated.count += 1; // add fail count on failure
        dataManager.UpdateUser(username, updated);
        tundraux::audit::logEvent("login", "failure " + username + " count=" + std::to_string(updated.count));
        colorcout("red", "Incorrect password for user " + username + ".\n");
        colorcout("red", "Failed attempts: " + std::to_string(updated.count) + "\n");
        colorcout("blue", "Password Hint: " + (it->password_hint.empty() ? "(none)" : it->password_hint) + "\n");
    }
}

void handleExitCommand(const std::string&) {
    exit(0);
}

void handleTimeCommand(const std::string&) {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::tm local_tm{};
    localtime_s(&local_tm, &tt);
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    colorcout("white", "Current time: " + oss.str() + "\n");
    colorcout("white", "Timestamp: " + std::to_string(ts) + "\n");
}

void handleModifyCommand(
    const std::string&,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    open_account_settings(currentUser, backendRuntime);
}

void renderShellHeader() {
    clear_screen();
    print_icon();
    colorcout("", "\n");
    colorcout("grey", "Tip: type help to view available commands.\n");
}

void handleClearScreenCommand(const std::string&) {
    renderShellHeader();
}

void handleLogoutCommand(
    const std::string&,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    if (currentUser.name.empty())
    {
        colorcout("yellow", "No user is currently logged in.\n");
        return;
    }

    if (usesBackend(backendRuntime)) {
        tundraux::audit::logEvent("logout", "backend");
        auto* client = backendRuntime->client();
        if (client == nullptr) {
            colorcout("red", "Backend unavailable.\n");
            return;
        }
        if (backendRuntime->sessionId().empty()) {
            colorcout("yellow", "No backend session is active.\n");
            return;
        }

        const auto result = client->logout(backendRuntime->sessionId());
        if (!result.ok) {
            colorcout("yellow", backendFailureMessage("Logout request failed.", result.errorCode) + "\n");
            return;
        }
        backendRuntime->setSessionId("");
        currentUser = guestUser();
        tundraux::audit::setCurrentUser(currentUser);
        if (!refreshBackendGuestSession(*backendRuntime)) {
            colorcout("yellow", "Logged out, but backend guest session recovery failed.\n");
            return;
        }
        colorcout("green", "Logged out successfully.\n");
        return;
    }

    tundraux::audit::logEvent("logout", currentUser.name);
    colorcout("green", "User " + currentUser.name + " logged out successfully.\n");
    currentUser = guestUser();
    tundraux::audit::setCurrentUser(currentUser);
}

void handleListUserCommand(
    const std::string&,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    if (usesBackend(backendRuntime)) {
        auto* client = backendRuntime->client();
        if (client == nullptr) {
            colorcout("red", "Backend unavailable.\n");
            return;
        }
        if (backendRuntime->sessionId().empty()) {
            colorcout("yellow", "No backend session is active.\n");
            return;
        }

        const auto result = client->listUsers(backendRuntime->sessionId());
        if (!result.ok) {
            colorcout(
                result.errorCode == "PermissionDenied" ? "red" : "yellow",
                backendFailureMessage("Unable to list users.", result.errorCode) + "\n"
            );
            return;
        }

        if (result.value.empty()) {
            colorcout("yellow", "No users found.\n");
            return;
        }

        colorcout("cyan", "Current Users:\n");
        for (const auto& user : result.value) {
            colorcout("white", "Username: " + user.name + " (" + user.type + ")\n");
        }
        return;
    }

    listUser();
}

void handleInfoCommand(const std::string&) {
    colorcout("cyan", "TundraUX 2.0 Build: " + std::string(tundraux::build_info::timestamp()) + "\n");
}

void handleManageUsersCommand(
    const std::string&,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    if (!usesBackend(backendRuntime)) {
        colorcout("red", "User management requires backend mode.\n");
        return;
    }
    manage_users(currentUser, backendRuntime);
}

void handleEditCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    tundraux::audit::setCurrentUser(currentUser);
    std::istringstream iss(input);
    std::string cmd, filename;
    iss >> cmd >> filename;
    if (filename.empty()) {
        tundraux::audit::logEvent("editor", "open (empty)");
        run_editor("", "");
        return;
    }
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lowerFilename.size() >= 4 &&
        lowerFilename.substr(lowerFilename.size() - 4) == ".tux") {
        colorcout("yellow", "Use explorer to open .TUX files.\n");
        return;
    }
    if (lowerFilename.size() >= 5 &&
        lowerFilename.substr(lowerFilename.size() - 5) == ".tlog") {
        colorcout("yellow", "Use explorer or export log to inspect encrypted audit logs.\n");
        return;
    }
    fs::path requestedPath(filename);
    if (requestedPath.is_absolute() || hasUnsafePathPart(requestedPath)) {
        colorcout("red", "Access Denied.\n");
        return;
    }

    if (usesBackend(backendRuntime)) {
        auto* client = backendRuntime->client();
        if (client == nullptr) {
            colorcout("red", "Backend unavailable.\n");
            return;
        }
        if (backendRuntime->sessionId().empty()) {
            colorcout("yellow", "No backend session is active.\n");
            return;
        }

        auto readResult = client->readFile(backendRuntime->sessionId(), filename);
        if (!readResult.ok && readResult.errorCode != "NotFound") {
            colorcout("red", backendFailureMessage("Unable to open file.", readResult.errorCode) + "\n");
            return;
        }

        TemporaryEditorFile tempFile;
        if (!tempFile.create(readResult.ok ? readResult.value : std::string{})) {
            colorcout("red", "Unable to prepare editor file.\n");
            return;
        }

        tundraux::audit::logEvent("editor", "backend open " + filename);
        const int editorResult = run_editor(tempFile.path().string(), filename);
        if (editorResult != 0) {
            return;
        }

        bool readTempOk = false;
        const std::string editedContent = readWholeFile(tempFile.path(), readTempOk);
        if (!readTempOk) {
            colorcout("red", "Unable to read editor output.\n");
            return;
        }

        auto writeResult = client->writeFile(backendRuntime->sessionId(), filename, editedContent);
        if (!writeResult.ok || !writeResult.value) {
            colorcout("red", backendFailureMessage("Unable to save file.", writeResult.errorCode) + "\n");
        }
        return;
    }

    const fs::path filesRoot = normalizeExistingPath(fs::current_path() / "Files");
    const fs::path targetPath = normalizeExistingPath(filesRoot / requestedPath);
    if (!isPathInsideRoot(targetPath, filesRoot)) {
        colorcout("red", "Access Denied.\n");
        return;
    }

    struct stat st;
    const std::string path = targetPath.string();
    if (stat(path.c_str(), &st) != 0) {
        colorcout("red", "Error: File not found: " + path + "\n");
        return;
    }
    tundraux::audit::logEvent("editor", "open " + path);
    run_editor(path, filename);
}

void handleExplorerCommand(
    const std::string&,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    tundraux::audit::setCurrentUser(currentUser);
    tundraux::audit::logEvent("explorer", "open");
    open_explorer(currentUser.name, currentUser.type, backendRuntime);
}

void handleWhoamiCommand(
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    if (!usesBackend(backendRuntime) || backendRuntime->sessionId().empty()) {
        displayLocalWhoami(currentUser);
        return;
    }

    auto* client = backendRuntime->client();
    if (client == nullptr) {
        colorcout("yellow", "Backend unavailable.\n");
        return;
    }

    const auto result = client->currentProfile(backendRuntime->sessionId());
    if (!result.ok) {
        colorcout("yellow", backendFailureMessage("Unable to query backend session.", result.errorCode) + "\n");
        return;
    }

    currentUser = shellUserFromBackend(result.value);
    tundraux::audit::setCurrentUser(currentUser);
    displayLocalWhoami(currentUser);
}

void handleStrictCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    std::istringstream iss(input);
    std::string command;
    std::string action;
    std::string extra;
    iss >> command >> action >> extra;

    if (!extra.empty()) {
        colorcout("yellow", "Usage: strict <status|on|off>\n");
        return;
    }

    if (usesBackend(backendRuntime)) {
        auto* client = backendRuntime->client();
        if (client == nullptr) {
            syncCurrentUserToGuest(currentUser);
            colorcout("red", "Backend unavailable.\n");
            return;
        }
        if (!ensureBackendSession(*backendRuntime)) {
            syncCurrentUserToGuest(currentUser);
            return;
        }

        syncCurrentUserFromBackend(currentUser, *backendRuntime);

        if (action.empty() || action == "status") {
            const auto strictResult = client->getStrictMode(backendRuntime->sessionId());
            if (!strictResult.ok) {
                if (strictResult.errorCode == "SessionExpired") {
                    recoverBackendGuestSession(currentUser, *backendRuntime);
                } else if (strictResult.errorCode == "PermissionDenied") {
                    syncCurrentUserFromBackend(currentUser, *backendRuntime);
                }
                colorcout("yellow", backendFailureMessage("Unable to query strict mode.", strictResult.errorCode) + "\n");
                return;
            }

            tundraux::audit::refreshStrictMode();
            colorcout("white", "Strict mode: " + std::string(strictResult.value ? "on" : "off") + "\n");
            tundraux::audit::logEvent("strict", "status " + std::string(strictResult.value ? "on" : "off"));
            return;
        }

        if (action != "on" && action != "off") {
            colorcout("yellow", "Usage: strict <status|on|off>\n");
            return;
        }

        const bool enabled = action == "on";
        const auto setResult = client->setStrictMode(backendRuntime->sessionId(), enabled);
        if (!setResult.ok) {
            if (setResult.errorCode == "SessionExpired") {
                recoverBackendGuestSession(currentUser, *backendRuntime);
            } else if (setResult.errorCode == "PermissionDenied") {
                syncCurrentUserFromBackend(currentUser, *backendRuntime);
            }
            colorcout("red", backendFailureMessage("Failed to update strict mode.", setResult.errorCode) + "\n");
            return;
        }

        tundraux::audit::setCurrentUser(currentUser);
        tundraux::audit::refreshStrictMode();
        tundraux::audit::logEvent("strict", enabled ? "enabled" : "disabled");
        colorcout("green", enabled ? "Strict mode enabled.\n" : "Strict mode disabled.\n");
        return;
    }

    DataManager dataManager("user_data.dat");
    if (action.empty() || action == "status") {
        const bool strictModeEnabled = dataManager.GetStrictMode();
        colorcout("white", "Strict mode: " + std::string(strictModeEnabled ? "on" : "off") + "\n");
        tundraux::audit::logEvent("strict", "status " + std::string(strictModeEnabled ? "on" : "off"));
        return;
    }

    if (action != "on" && action != "off") {
        colorcout("yellow", "Usage: strict <status|on|off>\n");
        return;
    }

    const bool enabled = action == "on";
    if (!dataManager.SetStrictMode(enabled)) {
        colorcout("red", "Failed to update strict mode.\n");
        return;
    }

    tundraux::audit::setCurrentUser(currentUser);
    tundraux::audit::refreshStrictMode();
    tundraux::audit::logEvent("strict", enabled ? "enabled" : "disabled");
    colorcout("green", enabled ? "Strict mode enabled.\n" : "Strict mode disabled.\n");
}

void handleExportCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    std::istringstream iss(input);
    std::string command;
    std::string subcommand;
    iss >> command >> subcommand;
    std::string path;
    std::getline(iss, path);
    path = trimLeadingSpaces(path);

    if (subcommand != "log" || path.empty()) {
        colorcout("yellow", "Usage: export log <tlog-file>\n");
        return;
    }

    if (usesBackend(backendRuntime)) {
        colorcout("red", "Export log is disabled in backend mode until it is served by backend RPC.\n");
        return;
    }

    std::string message;
    if (tundraux::audit::exportTlogToPlaintext(path, currentUser.name, currentUser.type, message)) {
        colorcout("green", message + "\n");
    } else {
        colorcout("red", message + "\n");
    }
}

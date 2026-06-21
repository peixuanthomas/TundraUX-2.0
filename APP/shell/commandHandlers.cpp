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
#include "backend_facade.hpp"
#include "backend_runtime.hpp"
#include "build_info.hpp"
#include "color.hpp"
#include "editor.hpp"
#include "explorer.hpp"
#include "manageusers.hpp"

namespace tundraux::audit {
bool exportTlogToPlaintext(
    const std::string& path,
    const std::string& currentUserName,
    const std::string& currentUserType,
    std::string& message
);
}
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

std::string auditApiPathFromInput(const std::string& path) {
    std::string normalized = fs::path(path).generic_u8string();
    const std::string lowerPath = [&] {
        std::string value = normalized;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }();

    constexpr const char* logsPrefix = "logs/";
    if (lowerPath.rfind(logsPrefix, 0) == 0) {
        normalized.erase(0, std::string(logsPrefix).size());
    }
    return normalized;
}

bool writeBackendExportedTlog(
    const tundraux::frontend::BackendRuntime& backendRuntime,
    const std::string& tlogPath,
    const std::string& content,
    std::string& message
) {
    message.clear();
    if (backendRuntime.filesRoot().empty()) {
        message = "Backend files root is unavailable.";
        return false;
    }

    std::string stem = fs::path(tlogPath).stem().string();
    if (stem.empty()) {
        stem = "audit";
    }

    const fs::path outputDir = fs::u8path(backendRuntime.filesRoot()) / "exported_logs";
    std::error_code dirError;
    fs::create_directories(outputDir, dirError);
    if (dirError) {
        message = "Failed to create export directory.";
        return false;
    }

    const fs::path outputPath = outputDir / (stem + ".log");
    std::error_code existsError;
    if (fs::exists(outputPath, existsError) && !existsError) {
        message = "Export target already exists.";
        return false;
    }
    if (existsError) {
        message = "Failed to inspect export target.";
        return false;
    }
    if (!writeWholeFile(outputPath, content)) {
        message = "Failed to write export file.";
        return false;
    }

    message = "Exported to " + outputPath.string();
    return true;
}

bool usesBackend(tundraux::frontend::BackendRuntime* backendRuntime) {
    return backendRuntime != nullptr && backendRuntime->client() != nullptr;
}

void setAuditCurrentUser(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const tundraux::frontend::ShellUser& currentUser
) {
    if (auditSink != nullptr) {
        auditSink->setCurrentUser(currentUser);
    }
}

void logAuditEvent(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const tundraux::frontend::ShellUser& currentUser,
    const std::string& category,
    const std::string& detail
) {
    if (auditSink == nullptr) {
        return;
    }
    setAuditCurrentUser(auditSink, currentUser);
    auditSink->logEvent(category, detail);
}

tundraux::frontend::ShellUser guestUser() {
    return tundraux::frontend::ShellUser{"guest", "", "", 0};
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

void displayLocalWhoami(const tundraux::frontend::ShellUser& currentUser) {
    if (currentUser.name.empty()) {
        colorcout("yellow", "No user is currently logged in.\n");
    } else {
        colorcout("white", "Current user: " + currentUser.name + " (" + currentUser.type + ")\n");
    }
}

void syncCurrentUserToGuest(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    currentUser = guestUser();
    setAuditCurrentUser(auditSink, currentUser);
}

bool syncCurrentUserFromBackend(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime& backendRuntime,
    tundraux::frontend::BackendFacade& facade,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    if (backendRuntime.client() == nullptr || backendRuntime.sessionId().empty()) {
        syncCurrentUserToGuest(currentUser, auditSink);
        return false;
    }

    const auto profile = facade.refreshProfile();
    if (profile.ok) {
        currentUser = profile.value;
        setAuditCurrentUser(auditSink, currentUser);
        return true;
    }

    syncCurrentUserToGuest(currentUser, auditSink);
    return false;
}
}

void handleLoginCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
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
        logAuditEvent(auditSink, currentUser, "login", "backend attempt");
        const std::string password = getHiddenInput("Please enter password: ", '*');
        tundraux::frontend::BackendFacade facade(*backendRuntime);
        const auto result = facade.login(username, password);
        if (!result.ok) {
            logAuditEvent(auditSink, currentUser, "login", "backend failure " + result.errorCode);
            colorcout(
                "red",
                backendFailureMessage("Unable to complete backend login.", result.errorCode, result.message) + "\n"
            );
            return;
        }

        currentUser = result.value;
        setAuditCurrentUser(auditSink, currentUser);
        logAuditEvent(auditSink, currentUser, "login", "backend success");
        rollcout("green", "Welcome, " + currentUser.name + "!");
        return;
    }

    colorcout("red", "Backend unavailable. Login requires the backend runtime.\n");
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
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    open_account_settings(currentUser, backendRuntime, auditSink);
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
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    if (currentUser.name.empty())
    {
        colorcout("yellow", "No user is currently logged in.\n");
        return;
    }

    if (usesBackend(backendRuntime)) {
        logAuditEvent(auditSink, currentUser, "logout", "backend");
        tundraux::frontend::BackendFacade facade(*backendRuntime);
        const auto result = facade.logout();
        if (!result.ok) {
            if (result.errorCode == "SessionExpired") {
                currentUser = guestUser();
                setAuditCurrentUser(auditSink, currentUser);
                colorcout("yellow", "Backend session expired. Logged out locally.\n");
                return;
            }
            colorcout("yellow", backendFailureMessage("Logout request failed.", result.errorCode) + "\n");
            return;
        }

        currentUser = guestUser();
        setAuditCurrentUser(auditSink, currentUser);
        if (!ensureBackendSession(*backendRuntime)) {
            colorcout("yellow", "Logged out, but backend guest session recovery failed.\n");
            return;
        }
        colorcout("green", "Logged out successfully.\n");
        return;
    }

    logAuditEvent(auditSink, currentUser, "logout", currentUser.name);
    colorcout("green", "User " + currentUser.name + " logged out successfully.\n");
    currentUser = guestUser();
    setAuditCurrentUser(auditSink, currentUser);
}

void handleListUserCommand(
    const std::string&,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink*
) {
    if (usesBackend(backendRuntime)) {
        tundraux::frontend::BackendFacade facade(*backendRuntime);
        const auto result = facade.listUsers();
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

    colorcout("red", "Backend unavailable. User listing requires the backend runtime.\n");
}

void handleInfoCommand(const std::string&) {
    colorcout("cyan", "TundraUX 2.0 Build: " + std::string(tundraux::build_info::timestamp()) + "\n");
}

void handleManageUsersCommand(
    const std::string&,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    if (!usesBackend(backendRuntime)) {
        colorcout("red", "User management requires backend mode.\n");
        return;
    }
    manage_users(currentUser, backendRuntime, auditSink);
}

void handleEditCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    setAuditCurrentUser(auditSink, currentUser);
    std::istringstream iss(input);
    std::string cmd, filename;
    iss >> cmd >> filename;
    if (filename.empty()) {
        logAuditEvent(auditSink, currentUser, "editor", "open (empty)");
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

        logAuditEvent(auditSink, currentUser, "editor", "backend open " + filename);
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
    logAuditEvent(auditSink, currentUser, "editor", "open " + path);
    run_editor(path, filename);
}

void handleExplorerCommand(
    const std::string&,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    setAuditCurrentUser(auditSink, currentUser);
    logAuditEvent(auditSink, currentUser, "explorer", "open");
    open_explorer(currentUser.name, currentUser.type, backendRuntime, auditSink);
}

void handleWhoamiCommand(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
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

    tundraux::frontend::BackendFacade facade(*backendRuntime);
    const auto result = facade.refreshProfile();
    if (!result.ok) {
        if (result.errorCode == "SessionExpired") {
            syncCurrentUserToGuest(currentUser, auditSink);
        }
        colorcout("yellow", backendFailureMessage("Unable to query backend session.", result.errorCode) + "\n");
        return;
    }

    currentUser = result.value;
    setAuditCurrentUser(auditSink, currentUser);
    displayLocalWhoami(currentUser);
}

void handleStrictCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
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
            syncCurrentUserToGuest(currentUser, auditSink);
            colorcout("red", "Backend unavailable.\n");
            return;
        }
        if (!ensureBackendSession(*backendRuntime)) {
            syncCurrentUserToGuest(currentUser, auditSink);
            return;
        }

        tundraux::frontend::BackendFacade facade(*backendRuntime);
        (void)syncCurrentUserFromBackend(currentUser, *backendRuntime, facade, auditSink);

        if (action.empty() || action == "status") {
            const auto strictResult = facade.getStrictMode();
            if (!strictResult.ok) {
                (void)syncCurrentUserFromBackend(currentUser, *backendRuntime, facade, auditSink);
                colorcout("yellow", backendFailureMessage("Unable to query strict mode.", strictResult.errorCode) + "\n");
                return;
            }

            colorcout("white", "Strict mode: " + std::string(strictResult.value ? "on" : "off") + "\n");
            logAuditEvent(auditSink, currentUser, "strict", "status " + std::string(strictResult.value ? "on" : "off"));
            return;
        }

        if (action != "on" && action != "off") {
            colorcout("yellow", "Usage: strict <status|on|off>\n");
            return;
        }

        const bool enabled = action == "on";
        const auto setResult = facade.setStrictMode(enabled);
        if (!setResult.ok) {
            (void)syncCurrentUserFromBackend(currentUser, *backendRuntime, facade, auditSink);
            colorcout("red", backendFailureMessage("Failed to update strict mode.", setResult.errorCode) + "\n");
            return;
        }

        setAuditCurrentUser(auditSink, currentUser);
        logAuditEvent(auditSink, currentUser, "strict", enabled ? "enabled" : "disabled");
        colorcout("green", enabled ? "Strict mode enabled.\n" : "Strict mode disabled.\n");
        return;
    }

    colorcout("red", "Backend unavailable. Strict mode requires the backend runtime.\n");
}

void handleExportCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
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
        tundraux::frontend::BackendFacade facade(*backendRuntime);
        const std::string backendPath = auditApiPathFromInput(path);
        const auto exported = facade.exportTlog(backendPath);
        if (!exported.ok) {
            colorcout("red", backendFailureMessage("Failed to export TLOG.", exported.errorCode, exported.message) + "\n");
            return;
        }

        std::string message;
        if (!writeBackendExportedTlog(*backendRuntime, backendPath, exported.value, message)) {
            colorcout("red", message + "\n");
            return;
        }

        logAuditEvent(auditSink, currentUser, "audit", "backend export " + backendPath);
        colorcout("green", message + "\n");
        return;
    }

    (void)auditSink;
    std::string message;
    if (tundraux::audit::exportTlogToPlaintext(path, currentUser.name, currentUser.type, message)) {
        colorcout("green", message + "\n");
    } else {
        colorcout("red", message + "\n");
    }
}

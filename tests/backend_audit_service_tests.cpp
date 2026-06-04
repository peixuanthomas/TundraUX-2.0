#include "audit_service.hpp"
#include "backend_error.hpp"
#include "session_service.hpp"
#include "user_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

class InMemoryUserStore final : public tundraux::backend::UserStore {
public:
    std::vector<tundraux::backend::BackendUser> users{
        {"admin", "alice", "Secret1", "hint", 0},
        {"user", "bob", "Secret2", "hint", 0}
    };

    std::vector<tundraux::backend::BackendUser> listUsers() const override {
        return users;
    }

    bool isStoreEmpty() const override {
        return users.empty();
    }

    bool addUser(const tundraux::backend::BackendUser& user) override {
        users.push_back(user);
        return true;
    }

    bool updateUser(const std::string& name, const tundraux::backend::BackendUser& user) override {
        for (auto& existing : users) {
            if (existing.name == name) {
                existing = user;
                return true;
            }
        }
        return false;
    }

    bool removeUser(const std::string& name) override {
        for (auto it = users.begin(); it != users.end(); ++it) {
            if (it->name == name) {
                users.erase(it);
                return true;
            }
        }
        return false;
    }

    bool getStrictMode() const override {
        return strictMode;
    }

    bool setStrictMode(bool enabled) override {
        strictMode = enabled;
        return true;
    }

    bool strictMode = false;
};

class TempDirectory {
public:
    explicit TempDirectory(std::filesystem::path root) : root_(std::move(root)) {
        std::error_code error;
        const auto tempRoot = std::filesystem::temp_directory_path(error);
        if (error) {
            throw std::runtime_error("unable to resolve temp directory");
        }
        const auto relative = std::filesystem::relative(root_, tempRoot, error);
        if (error || relative.empty() || relative.is_absolute() || relative.string().rfind("..", 0) == 0) {
            throw std::runtime_error("temporary directory must be within system temp directory");
        }
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& path() const {
        return root_;
    }

private:
    std::filesystem::path root_;
};

std::filesystem::path uniqueTempPath(const std::string& label) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("tundraux_backend_audit_service_" + label + "_" + std::to_string(ticks) + "_" + std::to_string(threadId));
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

std::string fileContents(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

std::string legacyObfuscate(std::string value) {
    for (char& ch : value) {
        ch ^= 0x55;
    }
    return value;
}

bool writeLegacyTlogRecord(const std::filesystem::path& path, const std::string& line) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write("TLOG1", 5);
    const std::string payload = legacyObfuscate(line);
    const std::size_t length = payload.size();
    out.write(reinterpret_cast<const char*>(&length), sizeof(length));
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

bool writesNothingWhenStrictModeOff() {
    TempDirectory temp(uniqueTempPath("off"));
    InMemoryUserStore users;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());
    const auto guestSession = sessions.startGuestSession();
    const auto login = sessions.login(guestSession.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "login should succeed for strict-mode-off test")) return false;

    const auto result = audit.logEvent(login.value.sessionId, "shell", "should not persist");
    if (!expect(result.ok, "logEvent should still return ok when strict mode is off")) return false;

    const auto logPath = temp.path() / "audit.tlog";
    return expect(!std::filesystem::exists(logPath), "audit log should not be created when strict mode is disabled");
}

bool writesEncryptedRecordWhenStrictModeOn() {
    TempDirectory temp(uniqueTempPath("on"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());
    const auto guestSession = sessions.startGuestSession();
    const auto login = sessions.login(guestSession.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "login should succeed for strict-mode-on test")) return false;

    const std::string detail = "secret: password=Secret1";
    const auto result = audit.logEvent(login.value.sessionId, "shell", detail);
    if (!expect(result.ok, "logEvent should succeed in strict mode")) return false;

    const auto logPath = temp.path() / "audit.tlog";
    if (!expect(std::filesystem::exists(logPath), "audit log should be created when strict mode is enabled")) return false;
    const std::string raw = fileContents(logPath);
    if (!expect(raw.rfind("TLOG1", 0) == 0, "audit log should have TLOG header")) return false;
    if (!expect(raw.find(detail) == std::string::npos, "audit log should not store plaintext detail")) return false;

    const auto read = audit.readTlog(login.value.sessionId, "audit.tlog");
    if (!expect(read.ok, "admin can read back encrypted record as plaintext")) return false;
    if (!expect(read.value.lines.size() == 1, "expected one decrypted audit line")) return false;
    return expect(
        read.value.lines[0].find("secret") != std::string::npos && read.value.lines[0].find("shell") != std::string::npos,
        "decrypted audit line should include category and detail context"
    );
}

bool readsLegacyDirectTlogAsPlaintext() {
    TempDirectory temp(uniqueTempPath("legacyRead"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());

    const std::string line = "2026-06-04 16:00:00 | user=alice | type=admin | shell | legacy direct log";
    if (!expect(writeLegacyTlogRecord(temp.path() / "legacy.tlog", line), "legacy tlog fixture should be written")) {
        return false;
    }

    const auto guestSession = sessions.startGuestSession();
    const auto login = sessions.login(guestSession.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "admin login should succeed for legacy tlog read")) return false;

    const auto read = audit.readTlog(login.value.sessionId, "legacy.tlog");
    if (!expect(read.ok, "admin should read legacy direct tlog")) return false;
    if (!expect(read.value.lines.size() == 1, "expected one legacy tlog line")) return false;
    return expect(read.value.lines[0] == line, "legacy tlog line should decrypt as plaintext");
}

bool guestAndUserCannotReadOrExportTlog() {
    TempDirectory temp(uniqueTempPath("ro"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());

    const auto adminGuest = sessions.startGuestSession();
    const auto adminLogin = sessions.login(adminGuest.sessionId, "alice", "Secret1");
    if (!expect(adminLogin.ok, "admin login should succeed")) return false;
    const auto wrote = audit.logEvent(adminLogin.value.sessionId, "shell", "write for readers");
    if (!expect(wrote.ok, "admin should be able to create log entries")) return false;

    const auto guest = sessions.startGuestSession();
    const auto guestRead = audit.readTlog(guest.sessionId, "audit.tlog");
    if (!expect(!guestRead.ok, "guest should not read tlog")) return false;
    if (!expect(
            guestRead.error.code == tundraux::backend::ErrorCode::PermissionDenied,
            "guest read should be denied")) return false;
    const auto guestExport = audit.exportTlog(guest.sessionId, "audit.tlog");
    if (!expect(!guestExport.ok, "guest should not export tlog")) return false;
    if (!expect(
            guestExport.error.code == tundraux::backend::ErrorCode::PermissionDenied,
            "guest export should be denied")) return false;

    const auto userGuest = sessions.startGuestSession();
    const auto userLogin = sessions.login(userGuest.sessionId, "bob", "Secret2");
    if (!expect(userLogin.ok, "user login should succeed")) return false;
    const auto userRead = audit.readTlog(userLogin.value.sessionId, "audit.tlog");
    if (!expect(!userRead.ok, "non-privileged user should not read tlog")) return false;
    if (!expect(
            userRead.error.code == tundraux::backend::ErrorCode::PermissionDenied,
            "user read should be denied")) return false;
    const auto userExport = audit.exportTlog(userLogin.value.sessionId, "audit.tlog");
    if (!expect(!userExport.ok, "non-privileged user should not export tlog")) return false;
    if (!expect(
            userExport.error.code == tundraux::backend::ErrorCode::PermissionDenied,
            "user export should be denied")) return false;

    return true;
}

bool guestCanAppendAuditButCannotReadOrExportTlog() {
    TempDirectory temp(uniqueTempPath("guestAppend"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());

    const auto guest = sessions.startGuestSession();
    const auto event = audit.logEvent(guest.sessionId, "login", "backend attempt");
    if (!expect(event.ok, "guest logEvent should append pre-login audit event")) return false;

    const auto key = audit.logKeyPress(guest.sessionId, "x", true);
    if (!expect(key.ok, "guest logKeyPress should append pre-login key audit event")) return false;

    const auto adminGuest = sessions.startGuestSession();
    const auto adminLogin = sessions.login(adminGuest.sessionId, "alice", "Secret1");
    if (!expect(adminLogin.ok, "admin login should succeed for guest append readback")) return false;
    const auto read = audit.readTlog(adminLogin.value.sessionId, "audit.tlog");
    if (!expect(read.ok, "admin should read guest audit records")) return false;
    if (!expect(read.value.lines.size() == 2, "expected guest event and key audit records")) return false;

    bool hasGuestEvent = false;
    bool hasRedactedKey = false;
    for (const auto& line : read.value.lines) {
        if (line.find("user=(none)") != std::string::npos &&
            line.find("type=guest") != std::string::npos &&
            line.find("login") != std::string::npos) {
            hasGuestEvent = true;
        }
        if (line.find("Character [redacted]") != std::string::npos) {
            hasRedactedKey = true;
        }
    }
    if (!expect(hasGuestEvent, "guest append should record guest identity")) return false;
    if (!expect(hasRedactedKey, "guest key append should preserve redacted key detail")) return false;

    const auto guestRead = audit.readTlog(guest.sessionId, "audit.tlog");
    if (!expect(!guestRead.ok, "guest still should not read tlog")) return false;
    if (!expect(
            guestRead.error.code == tundraux::backend::ErrorCode::PermissionDenied,
            "guest read should remain denied after guest append")) return false;
    const auto guestExport = audit.exportTlog(guest.sessionId, "audit.tlog");
    if (!expect(!guestExport.ok, "guest still should not export tlog")) return false;
    if (!expect(
            guestExport.error.code == tundraux::backend::ErrorCode::PermissionDenied,
            "guest export should remain denied after guest append")) return false;

    return true;
}

bool adminAndDebugCanReadPlaintext() {
    TempDirectory temp(uniqueTempPath("admin"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());

    const auto adminSession = sessions.startGuestSession();
    const auto adminLogin = sessions.login(adminSession.sessionId, "alice", "Secret1");
    if (!expect(adminLogin.ok, "admin login should succeed for plaintext test")) return false;

    const auto logged = audit.logKeyPress(adminLogin.value.sessionId, "a", false);
    if (!expect(logged.ok, "admin logKeyPress should succeed")) return false;
    const auto adminRead = audit.readTlog(adminLogin.value.sessionId, "audit.tlog");
    if (!expect(adminRead.ok, "admin read should succeed")) return false;
    if (!expect(adminRead.value.lines.size() == 1, "admin should see one decrypted line")) return false;
    if (!expect(adminRead.value.lines[0].find("Character 'a'") != std::string::npos,
            "admin should see plaintext key detail")) return false;

    const auto adminExport = audit.exportTlog(adminLogin.value.sessionId, "audit.tlog");
    if (!expect(adminExport.ok, "admin export should succeed")) return false;
    if (!expect(adminExport.value.content.find("Character 'a'") != std::string::npos,
            "admin export should include plaintext")) return false;

    const auto debugSession = sessions.startSession(tundraux::backend::BackendUser{"debug", "debug", "", "", 0});
    const auto debugRead = audit.readTlog(debugSession.sessionId, "audit.tlog");
    if (!expect(debugRead.ok, "debug read should succeed")) return false;
    if (!expect(debugRead.value.lines.size() == 1, "debug should see one decrypted line")) return false;
    if (!expect(debugRead.value.lines[0].find("Character 'a'") != std::string::npos,
            "debug read should include plaintext")) return false;
    return true;
}

bool disabledOrDeletedSessionUserDenied() {
    TempDirectory temp(uniqueTempPath("disabled"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());

    const auto guestSession = sessions.startGuestSession();
    const auto login = sessions.login(guestSession.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "user should log in for disabled/deleted test")) return false;
    const std::string sessionId = login.value.sessionId;

    for (auto& user : users.users) {
        if (user.name == "alice") {
            user.failedCount = 8;
            break;
        }
    }
    const auto disabled = audit.logEvent(sessionId, "shell", "should deny");
    if (!expect(!disabled.ok, "disabled user should be denied")) return false;
    if (!expect(
            disabled.error.code == tundraux::backend::ErrorCode::PermissionDenied,
            "disabled user should return PermissionDenied")) return false;

    users.removeUser("alice");
    const auto deleted = audit.logEvent(sessionId, "shell", "should not find");
    if (!expect(!deleted.ok, "deleted user should be denied")) return false;
    if (!expect(
            deleted.error.code == tundraux::backend::ErrorCode::NotFound,
            "deleted user should return NotFound")) return false;

    return true;
}

bool logsEscapeControlCharactersInCategoryAndDetail() {
    TempDirectory temp(uniqueTempPath("controls"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());

    const auto guestSession = sessions.startGuestSession();
    const auto login = sessions.login(guestSession.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "user should log in for control-character escape test")) return false;

    std::string detail = "first line\nsecond line\twith tab and";
    detail.push_back('\x1f');
    detail += "control";

    const auto result = audit.logEvent(
        login.value.sessionId,
        "shell\ndeep",
        detail
    );
    if (!expect(result.ok, "logEvent should accept control characters")) return false;

    const auto read = audit.readTlog(login.value.sessionId, "audit.tlog");
    if (!expect(read.ok, "admin should read escaped record")) return false;
    if (!expect(read.value.lines.size() == 1, "expected exactly one logical line for escaped record")) return false;
    const auto& line = read.value.lines[0];
    if (!expect(line.find('\n') == std::string::npos, "read line should not contain raw newline")) return false;
    if (!expect(line.find('\r') == std::string::npos, "read line should not contain raw carriage return")) return false;
    if (!expect(line.find("\\n") != std::string::npos, "escaped newline sequence should be visible")) return false;
    if (!expect(line.find("\\t") != std::string::npos, "escaped tab sequence should be visible")) return false;

    const auto exported = audit.exportTlog(login.value.sessionId, "audit.tlog");
    if (!expect(exported.ok, "admin should export escaped record")) return false;
    if (!expect(exported.value.content.find('\n') == std::string::npos, "exported content should keep single-record line intact")) return false;
    if (!expect(exported.value.content.find("\\n") != std::string::npos, "exported content should preserve newline escapes")) return false;
    if (!expect(exported.value.content.find("\\t") != std::string::npos, "exported content should preserve tab escapes")) return false;

    return true;
}

bool rejectsInvalidReadExportPaths() {
    TempDirectory temp(uniqueTempPath("pathValidation"));
    InMemoryUserStore users;
    users.strictMode = true;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::AuditService audit(users, sessions, temp.path().string());

    const auto guestSession = sessions.startGuestSession();
    const auto login = sessions.login(guestSession.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "user should log in for path validation")) return false;
    const auto logged = audit.logEvent(login.value.sessionId, "shell", "path validation");
    if (!expect(logged.ok, "log should be created for path validation")) return false;

    const auto absolutePath = (temp.path() / "audit.tlog").string();
    const auto badAbsoluteRead = audit.readTlog(login.value.sessionId, absolutePath);
    if (!expect(!badAbsoluteRead.ok, "absolute path should be rejected for readTlog")) return false;
    if (!expect(
            badAbsoluteRead.error.code == tundraux::backend::ErrorCode::InvalidPath,
            "absolute readTlog path should return InvalidPath")) return false;
    const auto badAbsoluteExport = audit.exportTlog(login.value.sessionId, absolutePath);
    if (!expect(!badAbsoluteExport.ok, "absolute path should be rejected for exportTlog")) return false;
    if (!expect(
            badAbsoluteExport.error.code == tundraux::backend::ErrorCode::InvalidPath,
            "absolute exportTlog path should return InvalidPath")) return false;

    const auto traversalRead = audit.readTlog(login.value.sessionId, "../audit.tlog");
    if (!expect(!traversalRead.ok, "traversal path should be rejected for readTlog")) return false;
    if (!expect(
            traversalRead.error.code == tundraux::backend::ErrorCode::InvalidPath,
            "traversal readTlog path should return InvalidPath")) return false;

    const auto traversalExport = audit.exportTlog(login.value.sessionId, "../audit.tlog");
    if (!expect(!traversalExport.ok, "traversal path should be rejected for exportTlog")) return false;
    if (!expect(
            traversalExport.error.code == tundraux::backend::ErrorCode::InvalidPath,
            "traversal exportTlog path should return InvalidPath")) return false;

    return true;
}

} // namespace

int main() {
    if (!writesNothingWhenStrictModeOff()) {
        return 1;
    }
    if (!writesEncryptedRecordWhenStrictModeOn()) {
        return 1;
    }
    if (!readsLegacyDirectTlogAsPlaintext()) {
        return 1;
    }
    if (!guestAndUserCannotReadOrExportTlog()) {
        return 1;
    }
    if (!guestCanAppendAuditButCannotReadOrExportTlog()) {
        return 1;
    }
    if (!adminAndDebugCanReadPlaintext()) {
        return 1;
    }
    if (!disabledOrDeletedSessionUserDenied()) {
        return 1;
    }
    if (!rejectsInvalidReadExportPaths()) {
        return 1;
    }
    if (!logsEscapeControlCharactersInCategoryAndDetail()) {
        return 1;
    }
    return 0;
}

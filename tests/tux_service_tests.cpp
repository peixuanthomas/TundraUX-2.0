#include "backend_error.hpp"
#include "filesystem_tux_store.hpp"
#include "session_service.hpp"
#include "tux_service.hpp"
#include "user_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

class InMemoryUserStore final : public tundraux::backend::UserStore {
public:
    explicit InMemoryUserStore(std::vector<tundraux::backend::BackendUser> users)
        : users_(std::move(users)) {}

    std::vector<tundraux::backend::BackendUser> listUsers() const override {
        return users_;
    }

    bool updateUser(const std::string& name, const tundraux::backend::BackendUser& user) override {
        for (auto& existing : users_) {
            if (existing.name == name) {
                existing = user;
                return true;
            }
        }
        return false;
    }

private:
    std::vector<tundraux::backend::BackendUser> users_;
};

class TempDirectory final {
public:
    explicit TempDirectory(std::filesystem::path root) : root_(std::move(root)) {
        verifyUnderTempRoot();
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    ~TempDirectory() {
        if (isUnderTempRoot()) {
            std::filesystem::remove_all(root_);
        }
    }

    const std::filesystem::path& path() const {
        return root_;
    }

private:
    void verifyUnderTempRoot() const {
        if (!isUnderTempRoot()) {
            throw std::runtime_error("TempDirectory root is outside system temp directory.");
        }
    }

    bool isUnderTempRoot() const {
        std::error_code error;
        const auto tempRoot = std::filesystem::temp_directory_path(error);
        if (error) {
            return false;
        }
        const auto relative = std::filesystem::relative(root_, tempRoot, error);
        if (error || relative.empty() || relative.is_absolute()) {
            return false;
        }
        const auto first = relative.begin();
        return first != relative.end() && *first != std::filesystem::path("..");
    }

    std::filesystem::path root_;
};

std::filesystem::path uniqueTempPath() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("tundraux_tux_service_tests_" + std::to_string(ticks) + "_" + std::to_string(threadId));
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool expectCode(
    const tundraux::backend::ServiceResult<tundraux::backend::EmptyResult>& result,
    tundraux::backend::ErrorCode code,
    const std::string& message
) {
    return expect(!result.ok, message + " should fail") &&
        expect(result.error.code == code, message + " code mismatch");
}

template <typename T>
bool expectResultCode(
    const tundraux::backend::ServiceResult<T>& result,
    tundraux::backend::ErrorCode code,
    const std::string& message
) {
    return expect(!result.ok, message + " should fail") &&
        expect(result.error.code == code, message + " code mismatch");
}

tundraux::backend::ServiceResult<tundraux::backend::SessionInfo> login(
    tundraux::backend::SessionService& sessions,
    const std::string& username,
    const std::string& password
) {
    const auto guest = sessions.startGuestSession();
    return sessions.login(guest.sessionId, username, password);
}

bool tux_creator_can_create_read_write_and_delete() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({{"user", "alice", "Password1", "hint", 0}});
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);

    const auto alice = login(sessions, "alice", "Password1");
    if (!expect(alice.ok, "alice login should pass")) return false;

    const auto created = service.create(alice.value.sessionId, "docs/note", false);
    const auto written = service.write(alice.value.sessionId, "docs/note", "hello");
    const auto read = service.read(alice.value.sessionId, "docs/note");
    const auto listed = service.list(alice.value.sessionId, "docs");
    const auto deleted = service.deleteFile(alice.value.sessionId, "docs/note");

    return expect(created.ok, "alice create should pass") &&
        expect(written.ok, "alice write should pass") &&
        expect(read.ok, "alice read should pass") &&
        expect(read.value.content == "hello", "alice read content mismatch") &&
        expect(listed.ok, "alice list should pass") &&
        expect(listed.value.size() == 1, "alice list count mismatch") &&
        expect(listed.value[0].name == "note", "alice list name mismatch") &&
        expect(listed.value[0].path == "docs/note", "alice list path mismatch") &&
        expect(deleted.ok, "alice delete should pass") &&
        expect(!std::filesystem::exists(temp.path() / "docs" / "note.TUX"), "deleted file should be gone");
}

bool tux_non_creator_is_denied() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({
        {"user", "alice", "Password1", "hint", 0},
        {"user", "bob", "Password1", "hint", 0}
    });
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);

    const auto alice = login(sessions, "alice", "Password1");
    const auto bob = login(sessions, "bob", "Password1");
    if (!expect(alice.ok, "alice login should pass")) return false;
    if (!expect(bob.ok, "bob login should pass")) return false;
    if (!expect(service.create(alice.value.sessionId, "shared/doc", false).ok, "alice create should pass")) return false;

    const auto bobRead = service.read(bob.value.sessionId, "shared/doc");
    const auto bobWrite = service.write(bob.value.sessionId, "shared/doc", "bob");

    return expectResultCode(bobRead, tundraux::backend::ErrorCode::PermissionDenied, "bob read") &&
        expectCode(bobWrite, tundraux::backend::ErrorCode::PermissionDenied, "bob write");
}

bool tux_admin_can_read_other_users_file() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({
        {"user", "alice", "Password1", "hint", 0},
        {"admin", "root", "Password1", "hint", 0}
    });
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);

    const auto alice = login(sessions, "alice", "Password1");
    const auto root = login(sessions, "root", "Password1");
    if (!expect(alice.ok, "alice login should pass")) return false;
    if (!expect(root.ok, "root login should pass")) return false;
    if (!expect(service.create(alice.value.sessionId, "shared/doc", false).ok, "alice create should pass")) return false;
    if (!expect(service.write(alice.value.sessionId, "shared/doc", "secret").ok, "alice write should pass")) return false;

    const auto read = service.read(root.value.sessionId, "shared/doc");

    return expect(read.ok, "admin read should pass") &&
        expect(read.value.content == "secret", "admin read content mismatch");
}

bool tux_guest_is_denied_for_list_and_create() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({{"user", "alice", "Password1", "hint", 0}});
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto guest = sessions.startGuestSession();

    const auto listed = service.list(guest.sessionId, "");
    const auto created = service.create(guest.sessionId, "docs/note", false);

    return expectResultCode(listed, tundraux::backend::ErrorCode::PermissionDenied, "guest list") &&
        expectCode(created, tundraux::backend::ErrorCode::PermissionDenied, "guest create");
}

bool corrupt_tux_file_returns_storage_error() {
    TempDirectory temp(uniqueTempPath());
    std::filesystem::create_directories(temp.path() / "docs");
    {
        std::ofstream out(temp.path() / "docs" / "bad.TUX", std::ios::binary);
        unsigned int version = 99;
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    }

    InMemoryUserStore users({{"admin", "root", "Password1", "hint", 0}});
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto root = login(sessions, "root", "Password1");
    if (!expect(root.ok, "root login should pass")) return false;

    const auto read = service.read(root.value.sessionId, "docs/bad");

    return expectResultCode(read, tundraux::backend::ErrorCode::StorageError, "corrupt read");
}

bool invalid_traversal_path_is_rejected() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({{"user", "alice", "Password1", "hint", 0}});
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto alice = login(sessions, "alice", "Password1");
    if (!expect(alice.ok, "alice login should pass")) return false;

    const auto read = service.read(alice.value.sessionId, "../secret");
    const auto create = service.create(alice.value.sessionId, "docs/../secret", false);

    return expectResultCode(read, tundraux::backend::ErrorCode::InvalidPath, "traversal read") &&
        expectCode(create, tundraux::backend::ErrorCode::InvalidPath, "traversal create");
}

} // namespace

int main() {
    if (!tux_creator_can_create_read_write_and_delete()) return 1;
    if (!tux_non_creator_is_denied()) return 1;
    if (!tux_admin_can_read_other_users_file()) return 1;
    if (!tux_guest_is_denied_for_list_and_create()) return 1;
    if (!corrupt_tux_file_returns_storage_error()) return 1;
    if (!invalid_traversal_path_is_rejected()) return 1;
    return 0;
}

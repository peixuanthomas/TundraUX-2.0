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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

bool expectBackendException(
    const std::string& message,
    tundraux::backend::ErrorCode expected,
    const std::function<void()>& action
) {
    try {
        action();
    } catch (const tundraux::backend::BackendException& error) {
        return expect(error.code() == expected, message + " code mismatch");
    }
    return expect(false, message + " should fail");
}

tundraux::backend::ServiceResult<tundraux::backend::SessionInfo> login(
    tundraux::backend::SessionService& sessions,
    const std::string& username,
    const std::string& password
) {
    const auto guest = sessions.startGuestSession();
    return sessions.login(guest.sessionId, username, password);
}

bool truncateLastByte(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0) {
        return false;
    }
    std::filesystem::resize_file(path, size - 1, error);
    return !error;
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

bool tux_regular_user_list_and_search_hide_other_users_files() {
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
    if (!expect(service.create(alice.value.sessionId, "shared/alice_doc", false).ok, "alice create should pass")) return false;
    if (!expect(service.create(bob.value.sessionId, "shared/bob_doc", false).ok, "bob create should pass")) return false;

    const auto listed = service.list(bob.value.sessionId, "shared");
    const auto searched = service.search(bob.value.sessionId, "shared", "doc");
    if (!expect(listed.ok, "bob filtered list should pass")) return false;
    if (!expect(searched.ok, "bob filtered search should pass")) return false;

    return expect(listed.value.size() == 1, "bob list should contain only own file") &&
        expect(listed.value[0].path == "shared/bob_doc", "bob list path mismatch") &&
        expect(searched.value.size() == 1, "bob search should contain only own file") &&
        expect(searched.value[0].path == "shared/bob_doc", "bob search path mismatch");
}

bool tux_regular_user_cannot_overwrite_other_users_destination() {
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
    if (!expect(service.create(alice.value.sessionId, "shared/alice_dest", false).ok, "alice dest create should pass")) return false;
    if (!expect(service.write(alice.value.sessionId, "shared/alice_dest", "alice").ok, "alice dest write should pass")) return false;
    if (!expect(service.create(bob.value.sessionId, "shared/bob_rename", false).ok, "bob rename source create should pass")) return false;
    if (!expect(service.create(bob.value.sessionId, "shared/bob_copy", false).ok, "bob copy source create should pass")) return false;
    if (!expect(service.create(bob.value.sessionId, "shared/bob_move", false).ok, "bob move source create should pass")) return false;

    const auto renamed = service.renameFile(bob.value.sessionId, "shared/bob_rename", "shared/alice_dest", true);
    const auto copied = service.copyFile(bob.value.sessionId, "shared/bob_copy", "shared/alice_dest", true);
    const auto moved = service.moveFile(bob.value.sessionId, "shared/bob_move", "shared/alice_dest", true);
    const auto aliceRead = service.read(alice.value.sessionId, "shared/alice_dest");

    return expectCode(renamed, tundraux::backend::ErrorCode::PermissionDenied, "bob overwrite rename") &&
        expectCode(copied, tundraux::backend::ErrorCode::PermissionDenied, "bob overwrite copy") &&
        expectCode(moved, tundraux::backend::ErrorCode::PermissionDenied, "bob overwrite move") &&
        expect(aliceRead.ok, "alice destination read should pass after denied overwrites") &&
        expect(aliceRead.value.content == "alice", "alice destination content should remain unchanged");
}

bool tux_store_rejects_symlink_traversal() {
    TempDirectory temp(uniqueTempPath());
    TempDirectory outside(uniqueTempPath());
    tundraux::backend::FilesystemTuxStore outsideStore(outside.path().string());
    outsideStore.create("secret", tundraux::backend::TuxMetadata{"alice", "alice", 1, 1}, false);

    std::error_code error;
    std::filesystem::create_directory_symlink(outside.path(), temp.path() / "link", error);
    if (error) {
        std::cerr << "Skipping TUX symlink traversal assertion: " << error.message() << "\n";
        return true;
    }

    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    bool rejected = false;
    try {
        (void)store.read("link/secret");
    } catch (const tundraux::backend::BackendException& failure) {
        rejected = failure.code() == tundraux::backend::ErrorCode::PermissionDenied ||
            failure.code() == tundraux::backend::ErrorCode::InvalidPath;
    }

    return expect(rejected, "TUX store should reject symlink traversal");
}

bool tux_unauthorized_read_and_copy_deny_before_content_parse() {
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
    if (!expect(service.create(alice.value.sessionId, "shared/corrupt", false).ok, "alice corrupt create should pass")) return false;
    if (!expect(truncateLastByte(temp.path() / "shared" / "corrupt.TUX"), "test should corrupt only content length")) return false;

    const auto bobRead = service.read(bob.value.sessionId, "shared/corrupt");
    const auto bobCopy = service.copyFile(bob.value.sessionId, "shared/corrupt", "shared/bob_copy", false);
    const auto aliceRead = service.read(alice.value.sessionId, "shared/corrupt");

    return expectResultCode(bobRead, tundraux::backend::ErrorCode::PermissionDenied, "bob corrupt read") &&
        expectCode(bobCopy, tundraux::backend::ErrorCode::PermissionDenied, "bob corrupt copy") &&
        expectResultCode(aliceRead, tundraux::backend::ErrorCode::StorageError, "alice corrupt read");
}

bool tux_temp_paths_are_rejected() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({{"user", "alice", "Password1", "hint", 0}});
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto alice = login(sessions, "alice", "Password1");
    if (!expect(alice.ok, "alice login should pass")) return false;
    if (!expect(service.create(alice.value.sessionId, "source", false).ok, "source create should pass")) return false;

    std::filesystem::create_directories(temp.path() / "temp");
    std::filesystem::rename(temp.path() / "source.TUX", temp.path() / "temp" / "existing.TUX");
    if (!expect(service.create(alice.value.sessionId, "source", false).ok, "source recreate should pass")) return false;

    const auto created = service.create(alice.value.sessionId, "temp/new", false);
    const auto read = service.read(alice.value.sessionId, "temp/existing");
    const auto written = service.write(alice.value.sessionId, "temp/existing", "x");
    const auto deleted = service.deleteFile(alice.value.sessionId, "temp/existing");
    const auto renamedFromTemp = service.renameFile(alice.value.sessionId, "temp/existing", "renamed", false);
    const auto copiedFromTemp = service.copyFile(alice.value.sessionId, "temp/existing", "copied", false);
    const auto movedFromTemp = service.moveFile(alice.value.sessionId, "temp/existing", "moved", false);
    const auto renamedToTemp = service.renameFile(alice.value.sessionId, "source", "temp/renamed", false);
    const auto copiedToTemp = service.copyFile(alice.value.sessionId, "source", "temp/copied", false);
    const auto movedToTemp = service.moveFile(alice.value.sessionId, "source", "temp/moved", false);

    return expectCode(created, tundraux::backend::ErrorCode::InvalidPath, "temp create") &&
        expectResultCode(read, tundraux::backend::ErrorCode::InvalidPath, "temp read") &&
        expectCode(written, tundraux::backend::ErrorCode::InvalidPath, "temp write") &&
        expectCode(deleted, tundraux::backend::ErrorCode::InvalidPath, "temp delete") &&
        expectCode(renamedFromTemp, tundraux::backend::ErrorCode::InvalidPath, "temp rename source") &&
        expectCode(copiedFromTemp, tundraux::backend::ErrorCode::InvalidPath, "temp copy source") &&
        expectCode(movedFromTemp, tundraux::backend::ErrorCode::InvalidPath, "temp move source") &&
        expectCode(renamedToTemp, tundraux::backend::ErrorCode::InvalidPath, "temp rename destination") &&
        expectCode(copiedToTemp, tundraux::backend::ErrorCode::InvalidPath, "temp copy destination") &&
        expectCode(movedToTemp, tundraux::backend::ErrorCode::InvalidPath, "temp move destination") &&
        expect(std::filesystem::exists(temp.path() / "source.TUX"), "denied temp move should keep source");
}

bool tux_rename_copy_move_success_paths_work() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({{"user", "alice", "Password1", "hint", 0}});
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto alice = login(sessions, "alice", "Password1");
    if (!expect(alice.ok, "alice login should pass")) return false;
    if (!expect(service.create(alice.value.sessionId, "ops/source", false).ok, "source create should pass")) return false;
    if (!expect(service.write(alice.value.sessionId, "ops/source", "payload").ok, "source write should pass")) return false;

    const auto copied = service.copyFile(alice.value.sessionId, "ops/source", "ops/copied", false);
    const auto renamed = service.renameFile(alice.value.sessionId, "ops/copied", "ops/renamed", false);
    const auto moved = service.moveFile(alice.value.sessionId, "ops/renamed", "archive/moved", false);
    const auto sourceRead = service.read(alice.value.sessionId, "ops/source");
    const auto movedRead = service.read(alice.value.sessionId, "archive/moved");

    return expect(copied.ok, "copy should pass") &&
        expect(renamed.ok, "rename should pass") &&
        expect(moved.ok, "move should pass") &&
        expect(sourceRead.ok, "source read should pass") &&
        expect(sourceRead.value.content == "payload", "copy should keep source content") &&
        expect(movedRead.ok, "moved read should pass") &&
        expect(movedRead.value.content == "payload", "moved content mismatch");
}

bool tux_debug_user_can_access_other_users_file() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({
        {"user", "alice", "Password1", "hint", 0},
        {"debug", "debugger", "Password1", "hint", 0}
    });
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);

    const auto alice = login(sessions, "alice", "Password1");
    const auto debug = login(sessions, "debugger", "Password1");
    if (!expect(alice.ok, "alice login should pass")) return false;
    if (!expect(debug.ok, "debug login should pass")) return false;
    if (!expect(service.create(alice.value.sessionId, "shared/debug_doc", false).ok, "alice create should pass")) return false;
    if (!expect(service.write(alice.value.sessionId, "shared/debug_doc", "debug-visible").ok, "alice write should pass")) return false;

    const auto read = service.read(debug.value.sessionId, "shared/debug_doc");

    return expect(read.ok, "debug read should pass") &&
        expect(read.value.content == "debug-visible", "debug read content mismatch");
}

bool tux_root_list_and_search_hide_mixed_case_temp() {
    TempDirectory temp(uniqueTempPath());
    std::filesystem::create_directories(temp.path() / "Temp");
    tundraux::backend::FilesystemTuxStore tempStore((temp.path() / "Temp").string());
    tempStore.create("hidden", tundraux::backend::TuxMetadata{"alice", "alice", 1, 1}, false);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());

    const auto entries = store.list("");
    const auto results = store.search("", "hidden");

    bool hasTempDirectory = false;
    for (const auto& entry : entries) {
        if (entry.name == "Temp") {
            hasTempDirectory = true;
        }
    }

    return expect(!hasTempDirectory, "root list should hide mixed-case Temp") &&
        expect(results.empty(), "root search should hide files under mixed-case Temp");
}

bool tux_reserved_device_components_are_rejected() {
    TempDirectory temp(uniqueTempPath());
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    const tundraux::backend::TuxMetadata metadata{"alice", "alice", 1, 1};

    return expectBackendException(
        "CON create should fail",
        tundraux::backend::ErrorCode::InvalidPath,
        [&store, &metadata]() { store.create("CON", metadata, false); }) &&
        expectBackendException(
        "NUL child create should fail",
        tundraux::backend::ErrorCode::InvalidPath,
        [&store, &metadata]() { store.create("docs/NUL", metadata, false); }) &&
        expectBackendException(
        "COM1 create should fail",
        tundraux::backend::ErrorCode::InvalidPath,
        [&store, &metadata]() { store.create("COM1", metadata, false); }) &&
        expectBackendException(
        "LPT9 create should fail",
        tundraux::backend::ErrorCode::InvalidPath,
        [&store, &metadata]() { store.create("archive/LPT9", metadata, false); });
}

bool tux_failed_overwrite_move_preserves_destination() {
#ifdef _WIN32
    TempDirectory temp(uniqueTempPath());
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    const tundraux::backend::TuxMetadata metadata{"alice", "alice", 1, 1};
    store.create("source", metadata, false);
    store.write("source", "source", metadata);
    store.create("destination", metadata, false);
    store.write("destination", "destination", metadata);

    const auto sourcePath = temp.path() / "source.TUX";
    const HANDLE lock = CreateFileW(
        sourcePath.wstring().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (lock == INVALID_HANDLE_VALUE) {
        std::cerr << "Skipping failed overwrite move assertion: unable to lock source.\n";
        return true;
    }

    bool rejected = false;
    try {
        store.moveFile("source", "destination", true);
    } catch (const tundraux::backend::BackendException& error) {
        rejected = error.code() == tundraux::backend::ErrorCode::StorageError;
    }
    CloseHandle(lock);

    const auto destination = store.read("destination");
    return expect(rejected, "locked overwrite move should fail with StorageError") &&
        expect(destination.content == "destination", "failed overwrite move should preserve destination content");
#else
    std::cerr << "Skipping failed overwrite move assertion: Windows-only source lock behavior.\n";
    return true;
#endif
}

} // namespace

int main() {
    if (!tux_creator_can_create_read_write_and_delete()) return 1;
    if (!tux_non_creator_is_denied()) return 1;
    if (!tux_admin_can_read_other_users_file()) return 1;
    if (!tux_guest_is_denied_for_list_and_create()) return 1;
    if (!corrupt_tux_file_returns_storage_error()) return 1;
    if (!invalid_traversal_path_is_rejected()) return 1;
    if (!tux_regular_user_list_and_search_hide_other_users_files()) return 1;
    if (!tux_regular_user_cannot_overwrite_other_users_destination()) return 1;
    if (!tux_store_rejects_symlink_traversal()) return 1;
    if (!tux_unauthorized_read_and_copy_deny_before_content_parse()) return 1;
    if (!tux_temp_paths_are_rejected()) return 1;
    if (!tux_rename_copy_move_success_paths_work()) return 1;
    if (!tux_debug_user_can_access_other_users_file()) return 1;
    if (!tux_root_list_and_search_hide_mixed_case_temp()) return 1;
    if (!tux_reserved_device_components_are_rejected()) return 1;
    if (!tux_failed_overwrite_move_preserves_destination()) return 1;
    return 0;
}

# Backend Phase 3 First Batch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Explorer and TUX File Manager first-batch file workflows behind the local backend API.

**Architecture:** Extend the existing backend core/adapters/stdio split instead of adding UI behavior to the backend. Add regular file mutation/search APIs to `FileService`, add a TUX-specific service with a filesystem-backed store, expose both through JSON-RPC and the typed frontend client, then route Explorer and TUX File Manager operations through small frontend backend facades.

**Tech Stack:** C++17, CMake, Windows console APIs, line-delimited JSON-RPC, existing TundraTUI input/rendering, existing `SYSTEM/crypto` XOR helper.

---

## File Structure

- Modify `BACKEND/core/file_store.hpp`: extend the regular managed-file store interface with mutation/search methods.
- Modify `BACKEND/core/file_service.hpp` and `BACKEND/core/file_service.cpp`: add session-aware wrappers for regular file mutation/search APIs.
- Create `BACKEND/core/tux_store.hpp`: backend-neutral TUX store interface and DTOs.
- Create `BACKEND/core/tux_service.hpp` and `BACKEND/core/tux_service.cpp`: session-aware TUX authorization and operation service.
- Create `BACKEND/adapters/filesystem_tux_store.hpp` and `BACKEND/adapters/filesystem_tux_store.cpp`: filesystem-backed `.TUX` reader/writer using the existing file format and `encryptDecrypt`.
- Modify `BACKEND/adapters/filesystem_file_store.hpp` and `BACKEND/adapters/filesystem_file_store.cpp`: implement regular file delete, rename, copy, move, mkdir, rmdir, and search.
- Modify `BACKEND/core/json_rpc.hpp` and `BACKEND/core/json_rpc.cpp`: accept `TuxService` and dispatch new `file.*` and `tux.*` methods.
- Modify `BACKEND/stdio/main.cpp`: construct and wire the filesystem TUX store/service.
- Modify `APP/backend_client/backend_client.hpp` and `APP/backend_client/backend_client.cpp`: add typed client DTOs and methods for new file and TUX APIs.
- Create `APP/explorer/explorer_backend.hpp` and `APP/explorer/explorer_backend.cpp`: frontend facade that maps Explorer UI state to backend client calls.
- Modify `APP/explorer/*.cpp` only where operations currently mutate or enumerate managed files.
- Create `APP/file_manager/tux_backend.hpp` and `APP/file_manager/tux_backend.cpp`: frontend facade for TUX File Manager commands.
- Modify `APP/file_manager/TUXfile.hpp` and `APP/file_manager/TUXfile.cpp`: route first-batch commands through the TUX backend facade while keeping command parsing and editor handoff local.
- Modify `APP/shell/commandHandlers.hpp`, `APP/shell/commandHandlers.cpp`, and `APP/shell/commandRegistry.cpp`: pass the backend runtime into Explorer and TUX File Manager entry points.
- Modify `CMakeLists.txt`: add new backend core/adapters/frontend sources and tests.
- Modify tests:
  - `tests/backend_file_service_tests.cpp`
  - `tests/backend_json_rpc_tests.cpp`
  - `tests/backend_stdio_tests.cpp`
  - `tests/frontend_backend_client_tests.cpp`
  - `tests/explorer_clipboard_tests.cpp`
  - add `tests/tux_service_tests.cpp`
  - add `tests/tux_frontend_command_tests.cpp`

## Task 1: Regular File API Contract

**Files:**
- Modify: `BACKEND/core/file_store.hpp`
- Modify: `BACKEND/core/file_service.hpp`
- Modify: `BACKEND/core/file_service.cpp`
- Test: `tests/backend_file_service_tests.cpp`

- [ ] **Step 1: Write failing regular file service tests**

Append focused service-level tests to `tests/backend_file_service_tests.cpp`. Use the existing test helpers in that file. If the file does not have a fake store, add this fake near the other test fixtures:

```cpp
class RecordingFileStore final : public tundraux::backend::FileStore {
public:
    std::vector<tundraux::backend::FileEntry> entries;
    std::string content;
    std::vector<std::string> calls;

    std::vector<tundraux::backend::FileEntry> listDirectory(const std::string& path) const override {
        const_cast<RecordingFileStore*>(this)->calls.push_back("list:" + path);
        return entries;
    }

    tundraux::backend::FileContent readFile(const std::string& path) const override {
        const_cast<RecordingFileStore*>(this)->calls.push_back("read:" + path);
        return {content};
    }

    void writeFile(const std::string& path, const std::string& value) override {
        calls.push_back("write:" + path + ":" + value);
        content = value;
    }

    void deleteFile(const std::string& path) override {
        calls.push_back("delete:" + path);
    }

    void renameFile(const std::string& from, const std::string& to, bool overwrite) override {
        calls.push_back("rename:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
    }

    void copyFile(const std::string& from, const std::string& to, bool overwrite) override {
        calls.push_back("copy:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
    }

    void moveFile(const std::string& from, const std::string& to, bool overwrite) override {
        calls.push_back("move:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
    }

    void createDirectory(const std::string& path) override {
        calls.push_back("mkdir:" + path);
    }

    void removeDirectory(const std::string& path, bool recursive) override {
        calls.push_back("rmdir:" + path + ":" + (recursive ? "1" : "0"));
    }

    std::vector<tundraux::backend::FileEntry> search(const std::string& root, const std::string& query) const override {
        const_cast<RecordingFileStore*>(this)->calls.push_back("search:" + root + ":" + query);
        return entries;
    }
};
```

Add tests:

```cpp
bool regular_file_mutations_require_user_session() {
    RecordingFileStore store;
    InMemoryUserStore users;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FileService service(store, sessions);
    const auto guest = sessions.startGuestSession();

    return expect(!service.deleteFile(guest.sessionId, "a.txt").ok, "guest delete should fail") &&
           expect(!service.createDirectory(guest.sessionId, "docs").ok, "guest mkdir should fail") &&
           expect(store.calls.empty(), "guest calls should not reach store");
}

bool regular_file_mutations_delegate_for_logged_in_user() {
    RecordingFileStore store;
    InMemoryUserStore users;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FileService service(store, sessions);
    const auto guest = sessions.startGuestSession();
    const auto loggedIn = sessions.login(guest.sessionId, "alice", "Secret1");

    const bool ok =
        service.deleteFile(loggedIn.value.sessionId, "old.txt").ok &&
        service.renameFile(loggedIn.value.sessionId, "old.txt", "new.txt", false).ok &&
        service.copyFile(loggedIn.value.sessionId, "new.txt", "copy.txt", true).ok &&
        service.moveFile(loggedIn.value.sessionId, "copy.txt", "archive/copy.txt", false).ok &&
        service.createDirectory(loggedIn.value.sessionId, "archive").ok &&
        service.removeDirectory(loggedIn.value.sessionId, "archive", false).ok &&
        service.search(loggedIn.value.sessionId, "", "copy").ok;

    return expect(ok, "logged-in file mutations should succeed") &&
           expect(store.calls.size() == 7, "expected seven delegated calls");
}
```

Register both tests in the test runner's `main()` using the file's existing pattern.

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
cmake --build build --target backend_file_service_tests
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: build fails because `FileStore` and `FileService` do not yet declare the new methods.

- [ ] **Step 3: Extend core interfaces**

Modify `BACKEND/core/file_store.hpp`:

```cpp
class FileStore {
public:
    virtual ~FileStore() = default;
    virtual std::vector<FileEntry> listDirectory(const std::string& path) const = 0;
    virtual FileContent readFile(const std::string& path) const = 0;
    virtual void writeFile(const std::string& path, const std::string& content) = 0;
    virtual void deleteFile(const std::string& path) = 0;
    virtual void renameFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual void copyFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual void moveFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual void createDirectory(const std::string& path) = 0;
    virtual void removeDirectory(const std::string& path, bool recursive) = 0;
    virtual std::vector<FileEntry> search(const std::string& root, const std::string& query) const = 0;
};
```

Modify `BACKEND/core/file_service.hpp` to add:

```cpp
ServiceResult<EmptyResult> deleteFile(const std::string& sessionId, const std::string& path);
ServiceResult<EmptyResult> renameFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
);
ServiceResult<EmptyResult> copyFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
);
ServiceResult<EmptyResult> moveFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
);
ServiceResult<EmptyResult> createDirectory(const std::string& sessionId, const std::string& path);
ServiceResult<EmptyResult> removeDirectory(const std::string& sessionId, const std::string& path, bool recursive);
ServiceResult<std::vector<FileEntry>> search(
    const std::string& sessionId,
    const std::string& root,
    const std::string& query
) const;
```

- [ ] **Step 4: Implement service wrappers**

In `BACKEND/core/file_service.cpp`, add a private helper near the existing methods:

```cpp
template <typename Func>
ServiceResult<EmptyResult> runFileMutation(Func func) {
    try {
        func();
        return ServiceResult<EmptyResult>::success(EmptyResult{});
    } catch (const BackendException& error) {
        return ServiceResult<EmptyResult>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    }
}
```

Add methods following this pattern:

```cpp
ServiceResult<EmptyResult> FileService::deleteFile(const std::string& sessionId, const std::string& path) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }
    return runFileMutation([&] { files_.deleteFile(path); });
}

ServiceResult<EmptyResult> FileService::renameFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }
    return runFileMutation([&] { files_.renameFile(from, to, overwrite); });
}
```

Add the remaining methods with these bodies:

```cpp
ServiceResult<EmptyResult> FileService::copyFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }
    return runFileMutation([&] { files_.copyFile(from, to, overwrite); });
}

ServiceResult<EmptyResult> FileService::moveFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }
    return runFileMutation([&] { files_.moveFile(from, to, overwrite); });
}

ServiceResult<EmptyResult> FileService::createDirectory(const std::string& sessionId, const std::string& path) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }
    return runFileMutation([&] { files_.createDirectory(path); });
}

ServiceResult<EmptyResult> FileService::removeDirectory(
    const std::string& sessionId,
    const std::string& path,
    bool recursive
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }
    return runFileMutation([&] { files_.removeDirectory(path, recursive); });
}

ServiceResult<std::vector<FileEntry>> FileService::search(
    const std::string& sessionId,
    const std::string& root,
    const std::string& query
) const {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<std::vector<FileEntry>>::failure(access.error.code, access.error.message);
    }
    try {
        return ServiceResult<std::vector<FileEntry>>::success(files_.search(root, query));
    } catch (const BackendException& error) {
        return ServiceResult<std::vector<FileEntry>>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    }
}
```

- [ ] **Step 5: Run focused tests**

Run:

```powershell
cmake --build build --target backend_file_service_tests
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: build still fails because `FilesystemFileStore` has not implemented the new pure virtual methods. Continue to Task 2 before committing.

## Task 2: Filesystem Regular File Operations

**Files:**
- Modify: `BACKEND/adapters/filesystem_file_store.hpp`
- Modify: `BACKEND/adapters/filesystem_file_store.cpp`
- Test: `tests/backend_file_service_tests.cpp`

- [ ] **Step 1: Write failing filesystem adapter tests**

Add tests to `tests/backend_file_service_tests.cpp`:

```cpp
bool filesystem_file_store_mutates_regular_files() {
    TempDirectory temp(uniqueTempPath());
    tundraux::backend::FilesystemFileStore store(temp.path().string());

    store.writeFile("docs/source.txt", "alpha");
    store.createDirectory("archive");
    store.copyFile("docs/source.txt", "archive/copy.txt", false);
    const auto copied = store.readFile("archive/copy.txt");
    store.renameFile("archive/copy.txt", "archive/renamed.txt", false);
    store.moveFile("archive/renamed.txt", "moved.txt", false);
    store.deleteFile("docs/source.txt");

    return expect(copied.content == "alpha", "copied content mismatch") &&
           expect(store.readFile("moved.txt").content == "alpha", "moved content mismatch");
}

bool filesystem_file_store_rejects_regular_file_conflicts() {
    TempDirectory temp(uniqueTempPath());
    tundraux::backend::FilesystemFileStore store(temp.path().string());
    store.writeFile("a.txt", "a");
    store.writeFile("b.txt", "b");
    store.createDirectory("not_empty");
    store.writeFile("not_empty/item.txt", "x");

    bool existingRejected = false;
    bool nonEmptyRejected = false;
    try {
        store.copyFile("a.txt", "b.txt", false);
    } catch (const tundraux::backend::BackendException& error) {
        existingRejected = error.code() == tundraux::backend::ErrorCode::AlreadyExists;
    }
    try {
        store.removeDirectory("not_empty", false);
    } catch (const tundraux::backend::BackendException& error) {
        nonEmptyRejected = error.code() == tundraux::backend::ErrorCode::Conflict;
    }

    return expect(existingRejected, "copy should reject existing destination") &&
           expect(nonEmptyRejected, "non-recursive rmdir should reject non-empty directory");
}

bool filesystem_file_store_searches_by_name() {
    TempDirectory temp(uniqueTempPath());
    tundraux::backend::FilesystemFileStore store(temp.path().string());
    store.writeFile("docs/alpha.txt", "a");
    store.writeFile("docs/beta.txt", "b");
    store.createDirectory("docs/alpha-folder");

    const auto results = store.search("docs", "alpha");
    return expect(results.size() == 2, "expected file and directory search matches");
}
```

Register the tests in `main()`.

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```powershell
cmake --build build --target backend_file_service_tests
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: build fails until adapter declarations and definitions exist.

- [ ] **Step 3: Declare adapter methods**

Add to `BACKEND/adapters/filesystem_file_store.hpp` public section:

```cpp
void deleteFile(const std::string& path) override;
void renameFile(const std::string& from, const std::string& to, bool overwrite) override;
void copyFile(const std::string& from, const std::string& to, bool overwrite) override;
void moveFile(const std::string& from, const std::string& to, bool overwrite) override;
void createDirectory(const std::string& path) override;
void removeDirectory(const std::string& path, bool recursive) override;
std::vector<FileEntry> search(const std::string& root, const std::string& query) const override;
```

Add private helper declarations:

```cpp
void rejectSamePath(const std::filesystem::path& from, const std::filesystem::path& to) const;
void rejectExistingDestination(const std::filesystem::path& destination, bool overwrite) const;
FileEntry entryFromPath(const std::filesystem::path& path) const;
```

- [ ] **Step 4: Implement adapter methods**

In `BACKEND/adapters/filesystem_file_store.cpp`, implement helpers:

```cpp
void FilesystemFileStore::rejectSamePath(const std::filesystem::path& from, const std::filesystem::path& to) const {
    if (stableAbsolutePath(from) == stableAbsolutePath(to)) {
        throw BackendException(ErrorCode::Conflict, "Source and destination are the same.");
    }
}

void FilesystemFileStore::rejectExistingDestination(const std::filesystem::path& destination, bool overwrite) const {
    std::error_code error;
    if (std::filesystem::exists(destination, error) && !overwrite) {
        throw BackendException(ErrorCode::AlreadyExists, "Destination already exists.");
    }
}

FileEntry FilesystemFileStore::entryFromPath(const std::filesystem::path& path) const {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        throw BackendException(ErrorCode::StorageError, "File storage error.");
    }
    FileEntry entry;
    entry.name = path.filename().string();
    entry.path = std::filesystem::relative(path, root_, error).generic_string();
    if (error) {
        throw BackendException(ErrorCode::StorageError, "File storage error.");
    }
    entry.type = std::filesystem::is_directory(status) ? FileEntryType::Directory : FileEntryType::File;
    if (std::filesystem::is_regular_file(status)) {
        entry.size = std::filesystem::file_size(path, error);
        if (error) {
            entry.size = 0;
        }
    }
    return entry;
}
```

Implement operations using existing `resolveManagedPath` and protection helpers:

```cpp
void FilesystemFileStore::deleteFile(const std::string& path) {
    const auto resolved = resolveManagedPath(path, false);
    rejectUnsafeExistingPathComponents(resolved);
    std::error_code error;
    if (!std::filesystem::exists(resolved, error)) {
        throw BackendException(ErrorCode::NotFound, "File not found.");
    }
    if (!std::filesystem::is_regular_file(resolved, error)) {
        throw BackendException(ErrorCode::InvalidPath, "Path is not a file.");
    }
    std::filesystem::remove(resolved, error);
    if (error) {
        throw BackendException(ErrorCode::StorageError, "File storage error.");
    }
}
```

Implement `renameFile` as `moveFile(from, to, overwrite)`. Implement `copyFile` with `std::filesystem::copy_file`. Implement `moveFile` with `std::filesystem::rename`, falling back to `copyFile` then `deleteFile` if rename returns a cross-device error. Implement `createDirectory` with `std::filesystem::create_directories`. Implement `removeDirectory` with `std::filesystem::remove` when `recursive == false` and `std::filesystem::remove_all` when `recursive == true`. Implement `search` with `recursive_directory_iterator`, case-insensitive substring matching on filenames, and `entryFromPath`.

- [ ] **Step 5: Run focused tests**

Run:

```powershell
cmake --build build --target backend_file_service_tests
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: `backend_file_service_tests` passes.

- [ ] **Step 6: Commit**

Run:

```powershell
git add BACKEND/core/file_store.hpp BACKEND/core/file_service.hpp BACKEND/core/file_service.cpp BACKEND/adapters/filesystem_file_store.hpp BACKEND/adapters/filesystem_file_store.cpp tests/backend_file_service_tests.cpp
git commit -m "feat: extend backend regular file operations"
```

## Task 3: TUX Backend Service And Store

**Files:**
- Create: `BACKEND/core/tux_store.hpp`
- Create: `BACKEND/core/tux_service.hpp`
- Create: `BACKEND/core/tux_service.cpp`
- Create: `BACKEND/adapters/filesystem_tux_store.hpp`
- Create: `BACKEND/adapters/filesystem_tux_store.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/tux_service_tests.cpp`

- [ ] **Step 1: Write failing TUX service tests**

Create `tests/tux_service_tests.cpp`:

```cpp
#include "filesystem_tux_store.hpp"
#include "session_service.hpp"
#include "tux_service.hpp"
#include "user_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

class TempDirectory {
public:
    explicit TempDirectory(fs::path path) : path_(std::move(path)) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

fs::path uniqueTempPath() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return fs::temp_directory_path() /
        ("tundraux_tux_service_tests_" + std::to_string(ticks) + "_" + std::to_string(threadId));
}

class InMemoryUserStore final : public tundraux::backend::UserStore {
public:
    explicit InMemoryUserStore(std::vector<tundraux::backend::BackendUser> initialUsers)
        : users(std::move(initialUsers)) {}

    std::vector<tundraux::backend::BackendUser> listUsers() const override {
        return users;
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

    std::vector<tundraux::backend::BackendUser> users;
};

tundraux::backend::BackendUser user(const std::string& name, const std::string& type, const std::string& password) {
    return tundraux::backend::BackendUser{type, name, password, "hint", 0};
}

bool tux_creator_can_create_read_write_and_delete() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({user("alice", "user", "Password1")});
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto guest = sessions.startGuestSession();
    const auto login = sessions.login(guest.sessionId, "alice", "Password1");

    const bool created = service.create(login.value.sessionId, "docs/note", false).ok;
    const bool wrote = service.write(login.value.sessionId, "docs/note", "hello").ok;
    const auto read = service.read(login.value.sessionId, "docs/note");
    const auto entries = service.list(login.value.sessionId, "docs");
    const bool deleted = service.deleteFile(login.value.sessionId, "docs/note").ok;

    return expect(created, "create failed") &&
           expect(wrote, "write failed") &&
           expect(read.ok && read.value.content == "hello", "read content mismatch") &&
           expect(entries.ok && entries.value.size() == 1, "list mismatch") &&
           expect(deleted, "delete failed");
}

bool tux_non_creator_is_denied() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({
        user("alice", "user", "Password1"),
        user("bob", "user", "Password1")
    });
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto aliceGuest = sessions.startGuestSession();
    const auto alice = sessions.login(aliceGuest.sessionId, "alice", "Password1");
    const auto bobGuest = sessions.startGuestSession();
    const auto bob = sessions.login(bobGuest.sessionId, "bob", "Password1");

    service.create(alice.value.sessionId, "shared/doc", false);
    const auto bobRead = service.read(bob.value.sessionId, "shared/doc");
    const auto bobWrite = service.write(bob.value.sessionId, "shared/doc", "blocked");

    return expect(!bobRead.ok && bobRead.error.code == tundraux::backend::ErrorCode::PermissionDenied, "bob read should be denied") &&
           expect(!bobWrite.ok && bobWrite.error.code == tundraux::backend::ErrorCode::PermissionDenied, "bob write should be denied");
}

bool tux_admin_can_read_other_users_file() {
    TempDirectory temp(uniqueTempPath());
    InMemoryUserStore users({
        user("alice", "user", "Password1"),
        user("root", "admin", "Password1")
    });
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FilesystemTuxStore store(temp.path().string());
    tundraux::backend::TuxService service(store, sessions);
    const auto aliceGuest = sessions.startGuestSession();
    const auto alice = sessions.login(aliceGuest.sessionId, "alice", "Password1");
    const auto adminGuest = sessions.startGuestSession();
    const auto admin = sessions.login(adminGuest.sessionId, "root", "Password1");

    service.create(alice.value.sessionId, "shared/doc", false);
    service.write(alice.value.sessionId, "shared/doc", "visible");
    const auto adminRead = service.read(admin.value.sessionId, "shared/doc");

    return expect(adminRead.ok && adminRead.value.content == "visible", "admin should read content");
}

int main() {
    int failures = 0;
    if (!tux_creator_can_create_read_write_and_delete()) ++failures;
    if (!tux_non_creator_is_denied()) ++failures;
    if (!tux_admin_can_read_other_users_file()) ++failures;
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Register test and verify failure**

Modify `CMakeLists.txt`:

```cmake
add_executable(tux_service_tests
    tests/tux_service_tests.cpp
)

target_include_directories(tux_service_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/BACKEND/core
        ${PROJECT_SOURCE_DIR}/BACKEND/adapters
)

target_link_libraries(tux_service_tests
    PRIVATE
        tundraux_backend_core
        tundraux_backend_adapters
)

target_compile_features(tux_service_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME tux_service_tests COMMAND tux_service_tests)
```

Run:

```powershell
cmake --build build --target tux_service_tests
ctest --test-dir build -R tux_service_tests --output-on-failure
```

Expected: build fails because TUX backend headers do not exist.

- [ ] **Step 3: Add TUX store interface**

Create `BACKEND/core/tux_store.hpp`:

```cpp
#pragma once

#include "backend_error.hpp"
#include "file_store.hpp"

#include <ctime>
#include <string>
#include <vector>

namespace tundraux::backend {

struct TuxMetadata {
    std::string creator;
    std::string lastEditor;
    std::time_t createTime = 0;
    std::time_t modifyTime = 0;
};

struct TuxContent {
    std::string content;
    TuxMetadata metadata;
};

class TuxStore {
public:
    virtual ~TuxStore() = default;
    virtual std::vector<FileEntry> list(const std::string& path) const = 0;
    virtual TuxMetadata metadata(const std::string& path) const = 0;
    virtual TuxContent read(const std::string& path) const = 0;
    virtual void create(const std::string& path, const TuxMetadata& metadata, bool overwrite) = 0;
    virtual void write(const std::string& path, const std::string& content, const TuxMetadata& metadata) = 0;
    virtual void deleteFile(const std::string& path) = 0;
    virtual void renameFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual void copyFile(const std::string& from, const std::string& to, const TuxMetadata& metadata, bool overwrite) = 0;
    virtual void moveFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual std::vector<FileEntry> search(const std::string& root, const std::string& query) const = 0;
};

} // namespace tundraux::backend
```

- [ ] **Step 4: Add TUX service**

Create `BACKEND/core/tux_service.hpp`:

```cpp
#pragma once

#include "session_service.hpp"
#include "tux_store.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class TuxService {
public:
    TuxService(TuxStore& store, const SessionService& sessions);

    ServiceResult<std::vector<FileEntry>> list(const std::string& sessionId, const std::string& path) const;
    ServiceResult<TuxContent> read(const std::string& sessionId, const std::string& path) const;
    ServiceResult<EmptyResult> create(const std::string& sessionId, const std::string& path, bool overwrite);
    ServiceResult<EmptyResult> write(const std::string& sessionId, const std::string& path, const std::string& content);
    ServiceResult<EmptyResult> deleteFile(const std::string& sessionId, const std::string& path);
    ServiceResult<EmptyResult> renameFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
    ServiceResult<EmptyResult> copyFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
    ServiceResult<EmptyResult> moveFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
    ServiceResult<std::vector<FileEntry>> search(const std::string& sessionId, const std::string& root, const std::string& query) const;

private:
    TuxStore& store_;
    const SessionService& sessions_;

    ServiceResult<BackendUser> requireTuxAccess(const std::string& sessionId) const;
    bool canAccess(const BackendUser& user, const TuxMetadata& metadata) const;
    TuxMetadata newMetadata(const BackendUser& user) const;
    TuxMetadata updatedMetadata(const BackendUser& user, TuxMetadata metadata) const;
};

} // namespace tundraux::backend
```

Create `BACKEND/core/tux_service.cpp` with access checks equivalent to the existing frontend rules:

```cpp
#include "tux_service.hpp"

#include <chrono>
#include <exception>

namespace tundraux::backend {
namespace {

constexpr const char* kAccessDeniedMessage = "Access denied.";
constexpr const char* kTuxStorageErrorMessage = "TUX storage error.";

bool privileged(const BackendUser& user) {
    return user.type == "admin" || user.type == "debug";
}

template <typename T, typename Func>
ServiceResult<T> runTux(Func func) {
    try {
        return ServiceResult<T>::success(func());
    } catch (const BackendException& error) {
        return ServiceResult<T>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<T>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    }
}

template <typename Func>
ServiceResult<EmptyResult> runTuxMutation(Func func) {
    try {
        func();
        return ServiceResult<EmptyResult>::success(EmptyResult{});
    } catch (const BackendException& error) {
        return ServiceResult<EmptyResult>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    }
}

} // namespace

TuxService::TuxService(TuxStore& store, const SessionService& sessions)
    : store_(store), sessions_(sessions) {}

ServiceResult<BackendUser> TuxService::requireTuxAccess(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<BackendUser>::failure(session.error.code, session.error.message);
    }
    if (session.value.type == "guest") {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    return session;
}

bool TuxService::canAccess(const BackendUser& user, const TuxMetadata& metadata) const {
    return privileged(user) || (!user.name.empty() && metadata.creator == user.name);
}

TuxMetadata TuxService::newMetadata(const BackendUser& user) const {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return TuxMetadata{user.name, user.name, now, now};
}

TuxMetadata TuxService::updatedMetadata(const BackendUser& user, TuxMetadata metadata) const {
    metadata.lastEditor = user.name;
    metadata.modifyTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return metadata;
}
```

Add methods so `read`, `write`, `deleteFile`, `renameFile`, `copyFile`, and `moveFile` load metadata first and return `PermissionDenied` when `canAccess` is false. `create` uses `newMetadata`. `write` uses `updatedMetadata`.

- [ ] **Step 5: Add filesystem TUX store**

Create `BACKEND/adapters/filesystem_tux_store.hpp`:

```cpp
#pragma once

#include "tux_store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tundraux::backend {

class FilesystemTuxStore final : public TuxStore {
public:
    explicit FilesystemTuxStore(std::string root);

    std::vector<FileEntry> list(const std::string& path) const override;
    TuxMetadata metadata(const std::string& path) const override;
    TuxContent read(const std::string& path) const override;
    void create(const std::string& path, const TuxMetadata& metadata, bool overwrite) override;
    void write(const std::string& path, const std::string& content, const TuxMetadata& metadata) override;
    void deleteFile(const std::string& path) override;
    void renameFile(const std::string& from, const std::string& to, bool overwrite) override;
    void copyFile(const std::string& from, const std::string& to, const TuxMetadata& metadata, bool overwrite) override;
    void moveFile(const std::string& from, const std::string& to, bool overwrite) override;
    std::vector<FileEntry> search(const std::string& root, const std::string& query) const override;

private:
    std::filesystem::path configuredRoot_;
    std::filesystem::path root_;

    std::filesystem::path resolveTuxPath(const std::string& path, bool allowRoot) const;
    void writeFullFile(const std::filesystem::path& path, const std::string& content, const TuxMetadata& metadata) const;
    TuxContent readFullFile(const std::filesystem::path& path) const;
};

} // namespace tundraux::backend
```

Create `BACKEND/adapters/filesystem_tux_store.cpp` by moving the `.TUX` binary read/write logic from `APP/file_manager/TUXfile.cpp` into backend adapter form. Include:

```cpp
#include "filesystem_tux_store.hpp"

#include "crypto.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace tundraux::backend {
namespace {

constexpr std::size_t kMaxTuxStringLength = 1024;
constexpr std::size_t kMaxTuxContentLength = 16 * 1024 * 1024;
constexpr unsigned int kTuxFormatVersion = 1;

void writeEncryptedString(std::ofstream& out, const std::string& value) {
    const std::string encrypted = encryptDecrypt(value);
    const std::size_t length = encrypted.size();
    out.write(reinterpret_cast<const char*>(&length), sizeof(length));
    out.write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
}

bool readEncryptedString(std::ifstream& in, std::uintmax_t& remaining, std::string& value, std::size_t maxLength) {
    std::size_t length = 0;
    if (remaining < sizeof(length)) {
        return false;
    }
    in.read(reinterpret_cast<char*>(&length), sizeof(length));
    if (!in || length > maxLength || length > remaining - sizeof(length)) {
        return false;
    }
    remaining -= sizeof(length);
    std::string encrypted(length, '\0');
    in.read(encrypted.data(), static_cast<std::streamsize>(length));
    if (!in) {
        return false;
    }
    remaining -= length;
    value = encryptDecrypt(encrypted);
    return true;
}

} // namespace
```

Implement path validation with this component rule, then append `.TUX` internally for non-root file paths:

```cpp
bool validTuxComponent(const std::string& component) {
    return !component.empty() &&
           std::all_of(component.begin(), component.end(), [](unsigned char ch) {
               return std::isalnum(ch) || ch == '-' || ch == '_';
           });
}

std::filesystem::path FilesystemTuxStore::resolveTuxPath(const std::string& path, bool allowRoot) const {
    if (path.empty()) {
        if (allowRoot) {
            return root_;
        }
        throw BackendException(ErrorCode::InvalidPath, "Invalid TUX path.");
    }
    std::filesystem::path requested(path);
    if (requested.is_absolute()) {
        throw BackendException(ErrorCode::InvalidPath, "Absolute paths are not allowed.");
    }
    std::filesystem::path target = root_;
    std::istringstream parts(path);
    std::string part;
    while (std::getline(parts, part, '/')) {
        if (!validTuxComponent(part)) {
            throw BackendException(ErrorCode::InvalidPath, "Invalid TUX path.");
        }
        target /= std::filesystem::u8path(part);
    }
    target += ".TUX";
    const auto normalized = target.lexically_normal();
    if (!isPathInside(root_, normalized)) {
        throw BackendException(ErrorCode::PermissionDenied, "Access denied.");
    }
    return normalized;
}
```

For invalid format reads, throw exactly:

```cpp
throw BackendException(ErrorCode::StorageError, "TUX file is corrupt or unsupported.");
```

- [ ] **Step 6: Add CMake sources**

Modify backend core source list:

```cmake
add_library(tundraux_backend_core
    BACKEND/core/file_service.cpp
    BACKEND/core/json.cpp
    BACKEND/core/json_rpc.cpp
    BACKEND/core/session_service.cpp
    BACKEND/core/tux_service.cpp
    BACKEND/core/user_service.cpp
)
```

Modify backend adapters source list:

```cmake
add_library(tundraux_backend_adapters
    BACKEND/adapters/data_manager_user_store.cpp
    BACKEND/adapters/filesystem_file_store.cpp
    BACKEND/adapters/filesystem_tux_store.cpp
)
```

- [ ] **Step 7: Run TUX service tests**

Run:

```powershell
cmake --build build --target tux_service_tests
ctest --test-dir build -R tux_service_tests --output-on-failure
```

Expected: `tux_service_tests` passes.

- [ ] **Step 8: Commit**

Run:

```powershell
git add BACKEND/core/tux_store.hpp BACKEND/core/tux_service.hpp BACKEND/core/tux_service.cpp BACKEND/adapters/filesystem_tux_store.hpp BACKEND/adapters/filesystem_tux_store.cpp tests/tux_service_tests.cpp CMakeLists.txt
git commit -m "feat: add backend tux service"
```

## Task 4: JSON-RPC And Stdio API Exposure

**Files:**
- Modify: `BACKEND/core/json_rpc.hpp`
- Modify: `BACKEND/core/json_rpc.cpp`
- Modify: `BACKEND/stdio/main.cpp`
- Modify: `tests/backend_json_rpc_tests.cpp`
- Modify: `tests/backend_stdio_tests.cpp`

- [ ] **Step 1: Write failing JSON-RPC tests**

Add to `tests/backend_json_rpc_tests.cpp` a flow that starts a session, logs in, creates a regular directory, writes and searches a file, then creates and reads a TUX file:

```cpp
bool file_and_tux_rpc_methods_dispatch() {
    InMemoryUserStore users({backendUser("alice", "user", "Password1")});
    SessionService sessions(users);
    UserService userService(sessions, users);
    InMemoryFileStore files;
    InMemoryTuxStore tux;
    FileService fileService(files, sessions);
    TuxService tuxService(tux, sessions);
    JsonRpcDispatcher dispatcher(sessions, userService, fileService, tuxService);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const std::string sessionId = extractSessionId(guestResponse);
    const std::string loginResponse = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Password1"}})"
    );
    const std::string userSession = extractSessionId(loginResponse);

    const std::string mkdirResponse = dispatcher.handleLine(
        R"({"id":"3","method":"file.createDirectory","params":{"sessionId":")" + userSession + R"(","path":"docs"}})"
    );
    const std::string tuxCreateResponse = dispatcher.handleLine(
        R"({"id":"4","method":"tux.create","params":{"sessionId":")" + userSession + R"(","path":"docs/secret","overwrite":false}})"
    );
    const std::string tuxWriteResponse = dispatcher.handleLine(
        R"({"id":"5","method":"tux.write","params":{"sessionId":")" + userSession + R"(","path":"docs/secret","content":"hello"}})"
    );
    const std::string tuxReadResponse = dispatcher.handleLine(
        R"({"id":"6","method":"tux.read","params":{"sessionId":")" + userSession + R"(","path":"docs/secret"}})"
    );

    return expect(hasOkResult(mkdirResponse), "mkdir response should be ok") &&
           expect(hasOkResult(tuxCreateResponse), "tux create response should be ok") &&
           expect(hasOkResult(tuxWriteResponse), "tux write response should be ok") &&
           expect(tuxReadResponse.find(R"("content":"hello")") != std::string::npos, "tux read content missing");
}
```

Use the helper style already present in the file. If `InMemoryTuxStore` is needed, implement it in the test file with the `TuxStore` interface.

- [ ] **Step 2: Run JSON-RPC test and verify failure**

Run:

```powershell
cmake --build build --target backend_json_rpc_tests
ctest --test-dir build -R backend_json_rpc_tests --output-on-failure
```

Expected: build fails because `JsonRpcDispatcher` has no `TuxService` constructor and no new method dispatch.

- [ ] **Step 3: Update dispatcher constructor and fields**

Modify `BACKEND/core/json_rpc.hpp`:

```cpp
#include "tux_service.hpp"

class JsonRpcDispatcher {
public:
    JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files, TuxService& tux);
    JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files);
    JsonRpcDispatcher(SessionService& sessions, UserService& users);

private:
    SessionService& sessions_;
    UserService& users_;
    FileService* files_ = nullptr;
    TuxService* tux_ = nullptr;
};
```

Modify `BACKEND/core/json_rpc.cpp` constructors:

```cpp
JsonRpcDispatcher::JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files, TuxService& tux)
    : sessions_(sessions), users_(users), files_(&files), tux_(&tux) {}
```

- [ ] **Step 4: Add JSON helpers and dispatch cases**

Add helper:

```cpp
bool optionalBoolParam(const JsonValue::Object& params, const std::string& name, bool defaultValue) {
    const auto found = params.find(name);
    if (found == params.end()) {
        return defaultValue;
    }
    if (found->second.type() != JsonValue::Type::Boolean) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asBoolean();
}
```

Add dispatch cases for regular file operations:

```cpp
if (files_ != nullptr && method == "file.createDirectory") {
    const auto result = files_->createDirectory(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "path")
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"ok", JsonValue::boolean(true)}});
}
```

Add dispatch cases for the remaining regular file methods with this shape:

```cpp
if (files_ != nullptr && method == "file.deleteFile") {
    const auto result = files_->deleteFile(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "path")
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"ok", JsonValue::boolean(true)}});
}

if (files_ != nullptr && method == "file.renameFile") {
    const auto result = files_->renameFile(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "from"),
        requiredStringParam(params, "to"),
        optionalBoolParam(params, "overwrite", false)
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"ok", JsonValue::boolean(true)}});
}

if (files_ != nullptr && method == "file.copyFile") {
    const auto result = files_->copyFile(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "from"),
        requiredStringParam(params, "to"),
        optionalBoolParam(params, "overwrite", false)
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"ok", JsonValue::boolean(true)}});
}

if (files_ != nullptr && method == "file.moveFile") {
    const auto result = files_->moveFile(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "from"),
        requiredStringParam(params, "to"),
        optionalBoolParam(params, "overwrite", false)
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"ok", JsonValue::boolean(true)}});
}

if (files_ != nullptr && method == "file.removeDirectory") {
    const auto result = files_->removeDirectory(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "path"),
        optionalBoolParam(params, "recursive", false)
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"ok", JsonValue::boolean(true)}});
}

if (files_ != nullptr && method == "file.search") {
    const auto result = files_->search(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "root"),
        requiredStringParam(params, "query")
    );
    if (!result.ok) throwIfFailed(result.error);
    JsonValue::Array entries;
    for (const auto& entry : result.value) {
        entries.push_back(fileEntryToJson(entry));
    }
    return JsonValue::object({{"entries", JsonValue::array(std::move(entries))}});
}
```

Add TUX dispatch cases using `tux_`:

```cpp
if (tux_ != nullptr && method == "tux.read") {
    const auto result = tux_->read(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "path")
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({
        {"content", JsonValue::string(result.value.content)},
        {"creator", JsonValue::string(result.value.metadata.creator)},
        {"lastEditor", JsonValue::string(result.value.metadata.lastEditor)}
    });
}
```

Add `tux.list`, `tux.create`, `tux.write`, `tux.delete`, `tux.rename`, `tux.copy`, `tux.move`, and `tux.search`.

- [ ] **Step 5: Wire stdio backend**

Modify `BACKEND/stdio/main.cpp` to include `filesystem_tux_store.hpp`, construct the store and service, and pass it to the dispatcher:

```cpp
tundraux::backend::FilesystemFileStore files(filesRoot);
tundraux::backend::FilesystemTuxStore tuxStore(filesRoot);
tundraux::backend::FileService fileService(files, sessions);
tundraux::backend::TuxService tuxService(tuxStore, sessions);
tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users, fileService, tuxService);
```

- [ ] **Step 6: Add stdio flow test**

Extend `tests/backend_stdio_tests.cpp` with a process test that sends:

```text
session.startGuestSession
session.login
file.createDirectory
tux.create
tux.write
tux.read
tux.search
tux.delete
```

Assert every response has matching `id`, no `error`, and `tux.read` returns the written content.

- [ ] **Step 7: Run RPC and stdio tests**

Run:

```powershell
cmake --build build --target backend_json_rpc_tests backend_stdio_tests
ctest --test-dir build -R "backend_json_rpc_tests|backend_stdio_tests" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 8: Commit**

Run:

```powershell
git add BACKEND/core/json_rpc.hpp BACKEND/core/json_rpc.cpp BACKEND/stdio/main.cpp tests/backend_json_rpc_tests.cpp tests/backend_stdio_tests.cpp
git commit -m "feat: expose file and tux rpc methods"
```

## Task 5: Frontend Typed Client

**Files:**
- Modify: `APP/backend_client/backend_client.hpp`
- Modify: `APP/backend_client/backend_client.cpp`
- Modify: `tests/frontend_backend_client_tests.cpp`

- [ ] **Step 1: Write failing typed client tests**

Add tests to `tests/frontend_backend_client_tests.cpp`:

```cpp
bool client_sends_file_mutation_requests() {
    FakeTransport transport;
    transport.responses.push(R"({"id":"1","result":{"ok":true}})");
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.createDirectory("session-1", "docs");
    const auto request = parseJsonObject(transport.requests.front());

    return expect(result.ok, "create directory result should be ok") &&
           expect(request->at("method").asString() == "file.createDirectory", "method mismatch");
}

bool client_parses_tux_read_response() {
    FakeTransport transport;
    transport.responses.push(R"({"id":"1","result":{"content":"hello","creator":"alice","lastEditor":"alice"}})");
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.readTux("session-1", "docs/note");

    return expect(result.ok, "read tux should be ok") &&
           expect(result.value.content == "hello", "content mismatch") &&
           expect(result.value.creator == "alice", "creator mismatch");
}
```

Register the tests in `main()`.

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```powershell
cmake --build build --target frontend_backend_client_tests
ctest --test-dir build -R frontend_backend_client_tests --output-on-failure
```

Expected: build fails because the client methods and DTOs are missing.

- [ ] **Step 3: Add client DTOs and declarations**

Modify `APP/backend_client/backend_client.hpp`:

```cpp
struct FrontendTuxContent {
    std::string content;
    std::string creator;
    std::string lastEditor;
};

ClientResult<bool> deleteFile(const std::string& sessionId, const std::string& path);
ClientResult<bool> renameFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
ClientResult<bool> copyFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
ClientResult<bool> moveFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
ClientResult<bool> createDirectory(const std::string& sessionId, const std::string& path);
ClientResult<bool> removeDirectory(const std::string& sessionId, const std::string& path, bool recursive);
ClientResult<std::vector<FrontendFileEntry>> searchFiles(const std::string& sessionId, const std::string& root, const std::string& query);

ClientResult<std::vector<FrontendFileEntry>> listTux(const std::string& sessionId, const std::string& path);
ClientResult<bool> createTux(const std::string& sessionId, const std::string& path, bool overwrite);
ClientResult<FrontendTuxContent> readTux(const std::string& sessionId, const std::string& path);
ClientResult<bool> writeTux(const std::string& sessionId, const std::string& path, const std::string& content);
ClientResult<bool> deleteTux(const std::string& sessionId, const std::string& path);
ClientResult<bool> renameTux(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
ClientResult<bool> copyTux(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
ClientResult<bool> moveTux(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
ClientResult<std::vector<FrontendFileEntry>> searchTux(const std::string& sessionId, const std::string& root, const std::string& query);
```

- [ ] **Step 4: Implement client methods**

Add helpers in `backend_client.cpp`:

```cpp
JsonValue::Object paramsWithPath(const std::string& sessionId, const std::string& path) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("path", JsonValue::string(path));
    return params;
}

FrontendTuxContent parseTuxContent(const JsonValue& value) {
    if (value.type() != JsonValue::Type::Object) {
        throw std::logic_error("expected tux content object");
    }
    const auto& object = value.asObject();
    return FrontendTuxContent{
        requiredStringField(object, "content"),
        requiredStringField(object, "creator"),
        requiredStringField(object, "lastEditor")
    };
}
```

Implement methods with the existing `sendRequest` pattern:

```cpp
ClientResult<bool> BackendClient::createDirectory(const std::string& sessionId, const std::string& path) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.createDirectory",
        paramsWithPath(sessionId, path),
        [](const JsonValue& result) {
            if (result.type() != JsonValue::Type::Object) {
                throw std::logic_error("expected result object");
            }
            return requiredBooleanField(result.asObject(), "ok");
        }
    );
}
```

Use parameter names `from`, `to`, `overwrite`, `recursive`, `root`, `query`, and `content` exactly as in Task 4.

- [ ] **Step 5: Run focused tests**

Run:

```powershell
cmake --build build --target frontend_backend_client_tests
ctest --test-dir build -R frontend_backend_client_tests --output-on-failure
```

Expected: `frontend_backend_client_tests` passes.

- [ ] **Step 6: Commit**

Run:

```powershell
git add APP/backend_client/backend_client.hpp APP/backend_client/backend_client.cpp tests/frontend_backend_client_tests.cpp
git commit -m "feat: add frontend file and tux client methods"
```

## Task 6: Explorer Backend Facade And Operation Migration

**Files:**
- Create: `APP/explorer/explorer_backend.hpp`
- Create: `APP/explorer/explorer_backend.cpp`
- Modify: `APP/explorer/explorer_types.hpp`
- Modify: `APP/explorer/explorer.cpp`
- Modify: `APP/explorer/explorer_directory.cpp`
- Modify: `APP/explorer/explorer_delete.cpp`
- Modify: `APP/explorer/explorer_folder_ops.cpp`
- Modify: `APP/explorer/explorer_clipboard.cpp`
- Modify: `APP/explorer/explorer_search.cpp`
- Modify: `APP/explorer/explorer_open.cpp`
- Modify: `APP/explorer/explorer.hpp`
- Modify: `APP/shell/commandHandlers.cpp`
- Modify: `APP/shell/commandRegistry.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/explorer_clipboard_tests.cpp`

- [ ] **Step 1: Add Explorer backend facade test**

Extend `tests/explorer_clipboard_tests.cpp` with a fake backend and a paste test:

```cpp
class FakeExplorerBackend final : public tundraux::explorer::ExplorerBackend {
public:
    std::vector<std::string> calls;

    tundraux::explorer::ExplorerBackendResult<std::vector<tundraux::explorer::FileEntry>> listDirectory(const std::string&) override {
        return {true, {}, "", ""};
    }
    tundraux::explorer::ExplorerBackendResult<bool> createDirectory(const std::string& path) override {
        calls.push_back("mkdir:" + path);
        return {true, true, "", ""};
    }
    tundraux::explorer::ExplorerBackendResult<bool> deletePath(const std::string& path, bool recursive) override {
        calls.push_back("delete:" + path + ":" + (recursive ? "1" : "0"));
        return {true, true, "", ""};
    }
    tundraux::explorer::ExplorerBackendResult<bool> copyPath(const std::string& from, const std::string& to, bool overwrite) override {
        calls.push_back("copy:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }
    tundraux::explorer::ExplorerBackendResult<bool> movePath(const std::string& from, const std::string& to, bool overwrite) override {
        calls.push_back("move:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }
    tundraux::explorer::ExplorerBackendResult<std::vector<tundraux::explorer::FileEntry>> search(const std::string&, const std::string&) override {
        return {true, {}, "", ""};
    }
};

bool explorer_paste_uses_backend_copy() {
    FakeExplorerBackend backend;
    tundraux::explorer::ExplorerState state;
    state.backend = &backend;
    state.rootPath = "Files";
    state.currentPath = "Files";
    state.clipboard.mode = tundraux::explorer::ClipboardMode::Copy;
    state.clipboard.path = "Files/source.txt";
    state.clipboard.name = "source.txt";
    state.clipboard.isDirectory = false;

    tundraux::explorer::pasteClipboard(state);

    return expect(!backend.calls.empty(), "backend copy should be called") &&
           expect(backend.calls.front().find("copy:") == 0, "expected copy call");
}
```

- [ ] **Step 2: Run test and verify failure**

Run:

```powershell
cmake --build build --target explorer_clipboard_tests
ctest --test-dir build -R explorer_clipboard_tests --output-on-failure
```

Expected: build fails because `ExplorerBackend` does not exist.

- [ ] **Step 3: Create Explorer backend interface**

Create `APP/explorer/explorer_backend.hpp`:

```cpp
#pragma once

#include "backend_client.hpp"
#include "explorer_types.hpp"

#include <string>
#include <vector>

namespace tundraux::explorer {

template <typename T>
struct ExplorerBackendResult {
    bool ok = false;
    T value{};
    std::string errorCode;
    std::string message;
};

class ExplorerBackend {
public:
    virtual ~ExplorerBackend() = default;
    virtual ExplorerBackendResult<std::vector<FileEntry>> listDirectory(const std::string& path) = 0;
    virtual ExplorerBackendResult<bool> createDirectory(const std::string& path) = 0;
    virtual ExplorerBackendResult<bool> deletePath(const std::string& path, bool recursive) = 0;
    virtual ExplorerBackendResult<bool> copyPath(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual ExplorerBackendResult<bool> movePath(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual ExplorerBackendResult<std::vector<FileEntry>> search(const std::string& root, const std::string& query) = 0;
};

class BackendClientExplorerBackend final : public ExplorerBackend {
public:
    BackendClientExplorerBackend(tundraux::frontend::BackendClient& client, std::string sessionId);

    ExplorerBackendResult<std::vector<FileEntry>> listDirectory(const std::string& path) override;
    ExplorerBackendResult<bool> createDirectory(const std::string& path) override;
    ExplorerBackendResult<bool> deletePath(const std::string& path, bool recursive) override;
    ExplorerBackendResult<bool> copyPath(const std::string& from, const std::string& to, bool overwrite) override;
    ExplorerBackendResult<bool> movePath(const std::string& from, const std::string& to, bool overwrite) override;
    ExplorerBackendResult<std::vector<FileEntry>> search(const std::string& root, const std::string& query) override;

private:
    tundraux::frontend::BackendClient& client_;
    std::string sessionId_;
};

std::string explorerRelativePath(const fs::path& root, const fs::path& path);

} // namespace tundraux::explorer
```

Create `APP/explorer/explorer_backend.cpp` to map client DTOs to Explorer DTOs and convert `ClientResult<T>` into `ExplorerBackendResult<T>`.

- [ ] **Step 4: Add backend pointer to Explorer state**

Modify `APP/explorer/explorer_types.hpp`:

```cpp
class ExplorerBackend;

struct ExplorerState {
    ExplorerBackend* backend = nullptr;
    fs::path rootPath;
    fs::path currentPath;
    // keep existing fields after this
};
```

- [ ] **Step 5: Route Explorer operations**

Modify operation files so they use `state.backend` when present:

```cpp
if (state.backend != nullptr) {
    const std::string relative = explorerRelativePath(state.rootPath, target);
    const auto result = state.backend->deletePath(relative, entry.isDirectory);
    if (!result.ok) {
        state.message = redMessage(result.message);
        return;
    }
    state.message = "Deleted " + deletedName;
    refresh(state);
    return;
}
```

Use these backend branches in the operation files:

```cpp
// explorer_directory.cpp refresh
if (state.backend != nullptr) {
    const auto result = state.backend->listDirectory(explorerRelativePath(state.rootPath, state.currentPath));
    if (!result.ok) {
        state.entries.clear();
        state.message = redMessage(result.message);
        return;
    }
    state.entries = result.value;
    state.message = std::to_string(state.entries.size()) + " item(s)";
    return;
}

// explorer_folder_ops.cpp createFolderFromInput
if (state.backend != nullptr) {
    const auto result = state.backend->createDirectory(explorerRelativePath(state.rootPath, target));
    if (!result.ok) {
        state.message = redMessage(result.message);
        return;
    }
    state.message = "Created folder " + folderName;
    refresh(state);
    return;
}

// explorer_clipboard.cpp pasteClipboard
if (state.backend != nullptr) {
    const std::string from = explorerRelativePath(state.rootPath, state.clipboard.path);
    const std::string to = explorerRelativePath(state.rootPath, target);
    const auto result = state.clipboard.mode == ClipboardMode::Copy
        ? state.backend->copyPath(from, to, false)
        : state.backend->movePath(from, to, false);
    if (!result.ok) {
        state.message = redMessage(result.message);
        return;
    }
    state.message = state.clipboard.mode == ClipboardMode::Copy ? "Copied " + state.clipboard.name : "Moved " + state.clipboard.name;
    state.clipboard = ClipboardState{};
    refresh(state);
    return;
}

// explorer_search.cpp runSearch
if (state.backend != nullptr) {
    const auto result = state.backend->search(explorerRelativePath(state.rootPath, search.rootPath), query);
    if (!result.ok) {
        search.error = result.message;
        state.message = redMessage(result.message);
        return;
    }
    search.results = result.value;
    state.message = std::to_string(search.results.size()) + " search result(s)";
    return;
}
```

Keep existing direct filesystem logic only as the legacy fallback when `state.backend == nullptr`.

- [ ] **Step 6: Pass backend from shell**

Modify `APP/explorer/explorer.hpp`:

```cpp
namespace tundraux::frontend { class BackendRuntime; }

void open_explorer(
    const std::string& username,
    const std::string& usertype,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
```

Modify `APP/explorer/explorer.cpp` to construct `BackendClientExplorerBackend` when a runtime client exists and set `state.backend`.

Modify `APP/shell/commandHandlers.cpp` and `APP/shell/commandRegistry.cpp` so `handleExplorerCommand` receives the backend runtime and passes it to `open_explorer`.

- [ ] **Step 7: Update CMake**

Add to `TUNDRAUX_APP_SOURCES`:

```cmake
APP/explorer/explorer_backend.cpp
```

Add `APP/backend_client/backend_client.cpp` and `APP/explorer/explorer_backend.cpp` to the relevant Explorer test target if the test directly links the backend facade.

- [ ] **Step 8: Run Explorer tests**

Run:

```powershell
cmake --build build --target explorer_clipboard_tests explorer_permissions_tests
ctest --test-dir build -R "explorer_clipboard_tests|explorer_permissions_tests" --output-on-failure
```

Expected: both Explorer test targets pass.

- [ ] **Step 9: Commit**

Run:

```powershell
git add APP/explorer APP/shell/commandHandlers.cpp APP/shell/commandHandlers.hpp APP/shell/commandRegistry.cpp CMakeLists.txt tests/explorer_clipboard_tests.cpp
git commit -m "feat: route explorer file operations through backend"
```

## Task 7: TUX File Manager Backend Facade And Command Migration

**Files:**
- Create: `APP/file_manager/tux_backend.hpp`
- Create: `APP/file_manager/tux_backend.cpp`
- Modify: `APP/file_manager/TUXfile.hpp`
- Modify: `APP/file_manager/TUXfile.cpp`
- Modify: `APP/shell/commandHandlers.cpp`
- Modify: `APP/shell/commandRegistry.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/tux_frontend_command_tests.cpp`

- [ ] **Step 1: Write failing TUX frontend command tests**

Create `tests/tux_frontend_command_tests.cpp`:

```cpp
#include "tux_backend.hpp"

#include <iostream>
#include <string>
#include <vector>

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

class FakeTuxBackend final : public tundraux::file_manager::TuxBackend {
public:
    std::vector<std::string> calls;

    tundraux::file_manager::TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> list(const std::string& path) override {
        calls.push_back("list:" + path);
        return {true, {}, "", ""};
    }
    tundraux::file_manager::TuxBackendResult<bool> create(const std::string& path, bool overwrite) override {
        calls.push_back("create:" + path + ":" + (overwrite ? "1" : "0"));
        return {true, true, "", ""};
    }
    tundraux::file_manager::TuxBackendResult<tundraux::frontend::FrontendTuxContent> read(const std::string& path) override {
        calls.push_back("read:" + path);
        return {true, {"content", "alice", "alice"}, "", ""};
    }
    tundraux::file_manager::TuxBackendResult<bool> write(const std::string& path, const std::string& content) override {
        calls.push_back("write:" + path + ":" + content);
        return {true, true, "", ""};
    }
};

bool tux_backend_facade_can_be_faked() {
    FakeTuxBackend backend;
    const auto create = backend.create("docs/note", false);
    const auto read = backend.read("docs/note");
    return expect(create.ok, "create should be ok") &&
           expect(read.ok && read.value.content == "content", "read should return content") &&
           expect(backend.calls.size() == 2, "expected two calls");
}

int main() {
    return tux_backend_facade_can_be_faked() ? 0 : 1;
}
```

- [ ] **Step 2: Register test and verify failure**

Add to `CMakeLists.txt`:

```cmake
add_executable(tux_frontend_command_tests
    tests/tux_frontend_command_tests.cpp
    APP/file_manager/tux_backend.cpp
    APP/backend_client/backend_client.cpp
)

target_include_directories(tux_frontend_command_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/APP/file_manager
        ${PROJECT_SOURCE_DIR}/APP/backend_client
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(tux_frontend_command_tests
    PRIVATE
        tundraux_backend_core
)

target_compile_features(tux_frontend_command_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME tux_frontend_command_tests COMMAND tux_frontend_command_tests)
```

Run:

```powershell
cmake --build build --target tux_frontend_command_tests
ctest --test-dir build -R tux_frontend_command_tests --output-on-failure
```

Expected: build fails because `tux_backend.hpp` does not exist.

- [ ] **Step 3: Create TUX backend facade**

Create `APP/file_manager/tux_backend.hpp`:

```cpp
#pragma once

#include "backend_client.hpp"

#include <string>
#include <vector>

namespace tundraux::file_manager {

template <typename T>
struct TuxBackendResult {
    bool ok = false;
    T value{};
    std::string errorCode;
    std::string message;
};

class TuxBackend {
public:
    virtual ~TuxBackend() = default;
    virtual TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> list(const std::string& path) = 0;
    virtual TuxBackendResult<bool> create(const std::string& path, bool overwrite) = 0;
    virtual TuxBackendResult<tundraux::frontend::FrontendTuxContent> read(const std::string& path) = 0;
    virtual TuxBackendResult<bool> write(const std::string& path, const std::string& content) = 0;
    virtual TuxBackendResult<bool> deleteFile(const std::string& path) = 0;
    virtual TuxBackendResult<bool> renameFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual TuxBackendResult<bool> copyFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual TuxBackendResult<bool> moveFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> search(const std::string& root, const std::string& query) = 0;
    virtual TuxBackendResult<bool> createDirectory(const std::string& path) = 0;
    virtual TuxBackendResult<bool> removeDirectory(const std::string& path, bool recursive) = 0;
};

class BackendClientTuxBackend final : public TuxBackend {
public:
    BackendClientTuxBackend(tundraux::frontend::BackendClient& client, std::string sessionId);
    TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> list(const std::string& path) override;
    TuxBackendResult<bool> create(const std::string& path, bool overwrite) override;
    TuxBackendResult<tundraux::frontend::FrontendTuxContent> read(const std::string& path) override;
    TuxBackendResult<bool> write(const std::string& path, const std::string& content) override;
    TuxBackendResult<bool> deleteFile(const std::string& path) override;
    TuxBackendResult<bool> renameFile(const std::string& from, const std::string& to, bool overwrite) override;
    TuxBackendResult<bool> copyFile(const std::string& from, const std::string& to, bool overwrite) override;
    TuxBackendResult<bool> moveFile(const std::string& from, const std::string& to, bool overwrite) override;
    TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> search(const std::string& root, const std::string& query) override;
    TuxBackendResult<bool> createDirectory(const std::string& path) override;
    TuxBackendResult<bool> removeDirectory(const std::string& path, bool recursive) override;
private:
    tundraux::frontend::BackendClient& client_;
    std::string sessionId_;
};

} // namespace tundraux::file_manager
```

Create `APP/file_manager/tux_backend.cpp` to call the typed client methods from Task 5 and convert results.

- [ ] **Step 4: Thread backend through TUX File Manager entry point**

Modify `APP/file_manager/TUXfile.hpp`:

```cpp
namespace tundraux::frontend { class BackendRuntime; }

void file_editor(
    const std::string& currentUsername,
    const std::string& currentUsertype,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
```

Modify `APP/file_manager/TUXfile.cpp` so `file_editor` constructs `BackendClientTuxBackend` when runtime exists and passes a `TuxBackend*` into command handlers.

- [ ] **Step 5: Route first-batch TUX commands**

For each first-batch command, add a backend branch before the legacy direct path:

```cpp
if (backend != nullptr) {
    const auto result = backend->create(f, false);
    if (!result.ok) {
        colorcout("RED", result.message + "\n");
        return;
    }
    colorcout("GREEN", "Created empty file: " + f + "\n\n");
    return;
}
```

Apply this to list, create, view, edit, delete, rename, copy, move, find, mkdir, and rmdir. Keep import, export, and metadata on the existing legacy path.

For edit, use:

```cpp
const auto readResult = backend->read(filename);
if (!readResult.ok) {
    colorcout("RED", readResult.message + "\n");
    return;
}
// write readResult.value.content to a controlled temp file, open the existing editor,
// read the temp file back, then call backend->write(filename, newContent).
```

Use the existing temp-file cleanup style already present in `TUXfile.cpp`.

- [ ] **Step 6: Pass runtime from shell**

Modify `handleTuxFileCommand` in `APP/shell/commandHandlers.cpp` and its declaration to receive `BackendRuntime*`. Modify `APP/shell/commandRegistry.cpp` to capture the runtime and pass it to the handler.

- [ ] **Step 7: Update CMake**

Add to `TUNDRAUX_APP_SOURCES`:

```cmake
APP/file_manager/tux_backend.cpp
```

- [ ] **Step 8: Run TUX frontend test**

Run:

```powershell
cmake --build build --target tux_frontend_command_tests
ctest --test-dir build -R tux_frontend_command_tests --output-on-failure
```

Expected: `tux_frontend_command_tests` passes.

- [ ] **Step 9: Commit**

Run:

```powershell
git add APP/file_manager APP/shell/commandHandlers.cpp APP/shell/commandHandlers.hpp APP/shell/commandRegistry.cpp CMakeLists.txt tests/tux_frontend_command_tests.cpp
git commit -m "feat: route tux file manager through backend"
```

## Task 8: Documentation And Full Verification

**Files:**
- Modify: `readme.md`
- Modify: `README.zh-CN.md`
- Test: all registered tests

- [ ] **Step 1: Update English README**

In `readme.md`, update the backend section to state:

```markdown
Phase 3 first-batch file migration moves Explorer and main TUX File Manager file operations through the local backend. Explorer refresh, folder creation, delete, copy/move paste, and search use backend APIs. TUX File Manager list/create/view/edit/delete/rename/copy/move/find/mkdir/rmdir use backend APIs.

TUX import/export/metadata commands, full audit API migration, HTTP transport, remote access, and shared daemon mode remain future work.
```

- [ ] **Step 2: Update Chinese README**

In `README.zh-CN.md`, add the equivalent content near the existing backend phase section. Preserve the file's current encoding. If terminal output shows mojibake, edit the same section only and verify with `git diff -- README.zh-CN.md`.

- [ ] **Step 3: Run full build**

Run:

```powershell
cmake --build build
```

Expected: build exits with code 0.

- [ ] **Step 4: Run full tests**

Run:

```powershell
ctest --test-dir build --output-on-failure
```

Expected: all registered tests pass, including:

```text
backend_file_service_tests
tux_service_tests
backend_json_rpc_tests
backend_stdio_tests
frontend_backend_client_tests
explorer_clipboard_tests
tux_frontend_command_tests
```

- [ ] **Step 5: Check migrated direct filesystem usage**

Run:

```powershell
rg -n "std::filesystem::(remove|remove_all|rename|copy|copy_file|create_directories)|std::ifstream|std::ofstream" APP\\explorer APP\\file_manager
```

Expected: remaining matches in migrated paths are limited to UI temp-file handoff, preview/rendering, legacy import/export/metadata, and legacy-direct fallback paths. No migrated Explorer or TUX first-batch command should mutate durable files directly when a backend runtime is active.

- [ ] **Step 6: Commit**

Run:

```powershell
git add readme.md README.zh-CN.md
git commit -m "docs: describe backend phase three first batch"
```

## Final Completion

- [ ] **Step 1: Run final status and verification**

Run:

```powershell
git status --short
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected:

```text
git status --short
```

prints no unstaged or uncommitted changes, unless the user has unrelated local work outside the files touched by this plan.

Expected build result: exit code 0.

Expected test result: all registered tests pass.

- [ ] **Step 2: Summarize final scope**

Report:

```text
Phase 3 first batch completed:
- backend regular file mutation/search APIs
- backend TUX first-batch APIs
- JSON-RPC and stdio exposure
- frontend typed client coverage
- Explorer first-batch backend routing
- TUX File Manager first-batch backend routing
- README status update
Remaining Phase 3 work:
- TUX import/export/metadata backend migration
- full audit query/export and backend-owned audit logging
```

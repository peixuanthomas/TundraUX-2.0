# Backend Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the local stdio backend the normal runtime boundary for TundraUX2 session/user commands and basic managed-file read/write/list operations.

**Architecture:** Add backend file-service interfaces in `BACKEND/core`, filesystem adapters in `BACKEND/adapters`, and JSON-RPC methods in the existing dispatcher. Add a typed frontend backend client under `APP/backend_client`, then migrate selected shell commands and plain editor file access to that client while retaining legacy direct mode for development comparison.

**Tech Stack:** C++17, CMake, Windows process/pipe APIs, line-delimited JSON-RPC, existing TundraTUI console shell, existing `ctest` suite.

---

## File Structure

- Create `BACKEND/core/file_store.hpp`: backend-core storage interface and DTOs for managed files.
- Create `BACKEND/core/file_service.hpp` and `BACKEND/core/file_service.cpp`: session-aware file permission checks and service methods.
- Create `BACKEND/adapters/filesystem_file_store.hpp` and `BACKEND/adapters/filesystem_file_store.cpp`: filesystem-backed implementation rooted at `Files`.
- Modify `BACKEND/core/json_rpc.hpp` and `BACKEND/core/json_rpc.cpp`: dispatch `file.listDirectory`, `file.readFile`, and `file.writeFile`.
- Modify `BACKEND/stdio/main.cpp`: construct `FilesystemFileStore` and `FileService`; parse `--files-root`.
- Create `APP/backend_client/backend_client.hpp` and `APP/backend_client/backend_client.cpp`: typed frontend API for backend operations.
- Create `APP/backend_client/backend_process.hpp` and `APP/backend_client/backend_process.cpp`: Windows stdio child process and line transport.
- Create `APP/backend_client/backend_runtime.hpp` and `APP/backend_client/backend_runtime.cpp`: process/client lifetime, runtime options, and current session ID.
- Modify `CORE/main/main.cpp`: parse `--legacy-direct` and `--backend-stdio <path>`, initialize backend runtime before shell.
- Modify `APP/shell/command.hpp`, `APP/shell/command.cpp`, `APP/shell/commandHandlers.hpp`, and `APP/shell/commandHandlers.cpp`: pass runtime context and migrate scoped shell commands.
- Modify `APP/shell/commandReg.cpp`: capture runtime context in migrated command handlers.
- Modify `CMakeLists.txt`: add new source files and tests.
- Create `tests/backend_file_service_tests.cpp`: direct backend service tests.
- Modify `tests/backend_json_rpc_tests.cpp`: JSON-RPC coverage for file methods.
- Modify `tests/backend_stdio_tests.cpp`: process-level file API smoke flow.
- Create `tests/frontend_backend_client_tests.cpp`: frontend client parsing/request behavior tests.
- Modify `readme.md` and `README.zh-CN.md`: document Phase 2 and remaining legacy areas.

---

### Task 1: Backend File Service Core

**Files:**
- Create: `BACKEND/core/file_store.hpp`
- Create: `BACKEND/core/file_service.hpp`
- Create: `BACKEND/core/file_service.cpp`
- Test: `tests/backend_file_service_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing file service tests**

Create `tests/backend_file_service_tests.cpp` with an in-memory `FileStore` fake and these cases:

```cpp
#include "file_service.hpp"
#include "session_service.hpp"
#include "user_store.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace tundraux::backend;

namespace {
struct MemoryUsers final : UserStore {
    std::vector<BackendUser> users{{"user", "alice", "Secret1", "", 0}};
    std::vector<BackendUser> listUsers() const override { return users; }
    bool updateUser(const std::string& name, const BackendUser& user) override {
        for (auto& existing : users) {
            if (existing.name == name) {
                existing = user;
                return true;
            }
        }
        return false;
    }
};

struct MemoryFiles final : FileStore {
    bool wrote = false;
    std::string lastPath;
    std::string lastContent;
    std::vector<FileEntry> entries{{"note.txt", "note.txt", FileEntryType::File, 5}};

    std::vector<FileEntry> listDirectory(const std::string& path) override {
        if (path != "") throw BackendException(ErrorCode::InvalidPath, "Invalid path.");
        return entries;
    }
    std::string readFile(const std::string& path) override {
        if (path == "missing.txt") throw BackendException(ErrorCode::NotFound, "File not found.");
        if (path == "secret.TUX") throw BackendException(ErrorCode::PermissionDenied, "Protected file.");
        return "hello";
    }
    void writeFile(const std::string& path, const std::string& content) override {
        if (path == "secret.TUX") throw BackendException(ErrorCode::PermissionDenied, "Protected file.");
        wrote = true;
        lastPath = path;
        lastContent = content;
    }
};

bool expect(bool value, const std::string& message) {
    if (!value) std::cerr << message << "\n";
    return value;
}
} // namespace

int main() {
    MemoryUsers users;
    MemoryFiles files;
    SessionService sessions(users);
    FileService service(files, sessions);

    const auto guest = sessions.startGuestSession();
    if (!expect(!service.listDirectory(guest.sessionId, "").ok, "guest list should fail")) return 1;
    if (!expect(service.listDirectory(guest.sessionId, "").error.code == ErrorCode::PermissionDenied, "guest list error")) return 1;

    const auto login = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "login should pass")) return 1;

    const auto listed = service.listDirectory(guest.sessionId, "");
    if (!expect(listed.ok, "user list should pass")) return 1;
    if (!expect(listed.value.size() == 1 && listed.value[0].path == "note.txt", "list result mismatch")) return 1;

    const auto read = service.readFile(guest.sessionId, "note.txt");
    if (!expect(read.ok && read.value.content == "hello", "read result mismatch")) return 1;

    const auto write = service.writeFile(guest.sessionId, "note.txt", "updated");
    if (!expect(write.ok && files.wrote && files.lastContent == "updated", "write result mismatch")) return 1;

    const auto protectedRead = service.readFile(guest.sessionId, "secret.TUX");
    if (!expect(!protectedRead.ok && protectedRead.error.code == ErrorCode::PermissionDenied, "protected read error")) return 1;

    const auto missingRead = service.readFile(guest.sessionId, "missing.txt");
    if (!expect(!missingRead.ok && missingRead.error.code == ErrorCode::NotFound, "missing read error")) return 1;

    return 0;
}
```

- [ ] **Step 2: Register and run the failing test**

Add this test target to `CMakeLists.txt`:

```cmake
add_executable(backend_file_service_tests
    tests/backend_file_service_tests.cpp
)

target_include_directories(backend_file_service_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(backend_file_service_tests
    PRIVATE
        tundraux_backend_core
)

target_compile_features(backend_file_service_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME backend_file_service_tests COMMAND backend_file_service_tests)
```

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: build fails because `file_service.hpp` and `file_store.hpp` do not exist.

- [ ] **Step 3: Implement the core file service interfaces**

Create `BACKEND/core/file_store.hpp`:

```cpp
#pragma once

#include "backend_error.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace tundraux::backend {

class BackendException final : public std::exception {
public:
    BackendException(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}
    const char* what() const noexcept override { return message_.c_str(); }
    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
private:
    ErrorCode code_;
    std::string message_;
};

enum class FileEntryType {
    File,
    Directory
};

struct FileEntry {
    std::string name;
    std::string path;
    FileEntryType type = FileEntryType::File;
    std::uintmax_t size = 0;
};

struct FileContent {
    std::string content;
};

class FileStore {
public:
    virtual ~FileStore() = default;
    virtual std::vector<FileEntry> listDirectory(const std::string& path) = 0;
    virtual std::string readFile(const std::string& path) = 0;
    virtual void writeFile(const std::string& path, const std::string& content) = 0;
};

} // namespace tundraux::backend
```

Create `BACKEND/core/file_service.hpp`:

```cpp
#pragma once

#include "file_store.hpp"
#include "session_service.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class FileService {
public:
    FileService(FileStore& files, const SessionService& sessions);

    ServiceResult<std::vector<FileEntry>> listDirectory(
        const std::string& sessionId,
        const std::string& path
    );
    ServiceResult<FileContent> readFile(
        const std::string& sessionId,
        const std::string& path
    );
    ServiceResult<EmptyResult> writeFile(
        const std::string& sessionId,
        const std::string& path,
        const std::string& content
    );

private:
    FileStore& files_;
    const SessionService& sessions_;

    ServiceResult<BackendUser> requireFileUser(const std::string& sessionId) const;
};

} // namespace tundraux::backend
```

Create `BACKEND/core/file_service.cpp`:

```cpp
#include "file_service.hpp"

#include <exception>
#include <utility>

namespace tundraux::backend {

FileService::FileService(FileStore& files, const SessionService& sessions)
    : files_(files), sessions_(sessions) {}

ServiceResult<BackendUser> FileService::requireFileUser(const std::string& sessionId) const {
    const auto user = sessions_.requireSession(sessionId);
    if (!user.ok) {
        return user;
    }
    if (user.value.type == "guest") {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, "Access denied.");
    }
    return user;
}

ServiceResult<std::vector<FileEntry>> FileService::listDirectory(
    const std::string& sessionId,
    const std::string& path
) {
    const auto user = requireFileUser(sessionId);
    if (!user.ok) {
        return ServiceResult<std::vector<FileEntry>>::failure(user.error.code, user.error.message);
    }
    try {
        return ServiceResult<std::vector<FileEntry>>::success(files_.listDirectory(path));
    } catch (const BackendException& error) {
        return ServiceResult<std::vector<FileEntry>>::failure(error.code(), error.message());
    } catch (const std::exception&) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, "File storage error.");
    }
}

ServiceResult<FileContent> FileService::readFile(
    const std::string& sessionId,
    const std::string& path
) {
    const auto user = requireFileUser(sessionId);
    if (!user.ok) {
        return ServiceResult<FileContent>::failure(user.error.code, user.error.message);
    }
    try {
        return ServiceResult<FileContent>::success(FileContent{files_.readFile(path)});
    } catch (const BackendException& error) {
        return ServiceResult<FileContent>::failure(error.code(), error.message());
    } catch (const std::exception&) {
        return ServiceResult<FileContent>::failure(ErrorCode::StorageError, "File storage error.");
    }
}

ServiceResult<EmptyResult> FileService::writeFile(
    const std::string& sessionId,
    const std::string& path,
    const std::string& content
) {
    const auto user = requireFileUser(sessionId);
    if (!user.ok) {
        return ServiceResult<EmptyResult>::failure(user.error.code, user.error.message);
    }
    try {
        files_.writeFile(path, content);
        return ServiceResult<EmptyResult>::success(EmptyResult{});
    } catch (const BackendException& error) {
        return ServiceResult<EmptyResult>::failure(error.code(), error.message());
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, "File storage error.");
    }
}

} // namespace tundraux::backend
```

Modify `tundraux_backend_core` in `CMakeLists.txt`:

```cmake
add_library(tundraux_backend_core
    BACKEND/core/file_service.cpp
    BACKEND/core/json.cpp
    BACKEND/core/json_rpc.cpp
    BACKEND/core/session_service.cpp
    BACKEND/core/user_service.cpp
)
```

- [ ] **Step 4: Run the file service test**

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: `backend_file_service_tests` passes.

- [ ] **Step 5: Commit**

```powershell
git add BACKEND/core/file_store.hpp BACKEND/core/file_service.hpp BACKEND/core/file_service.cpp tests/backend_file_service_tests.cpp CMakeLists.txt
git commit -m "feat: add backend file service core"
```

---

### Task 2: Filesystem File Store Adapter

**Files:**
- Create: `BACKEND/adapters/filesystem_file_store.hpp`
- Create: `BACKEND/adapters/filesystem_file_store.cpp`
- Test: extend `tests/backend_file_service_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add adapter tests**

Extend `tests/backend_file_service_tests.cpp` with a second test function that includes `filesystem_file_store.hpp` and creates a temp `Files` root:

```cpp
#include "filesystem_file_store.hpp"

#include <filesystem>
#include <fstream>

bool runFilesystemStoreTests() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "tundraux_backend_file_store_tests";
    fs::remove_all(root);
    fs::create_directories(root / "docs");
    {
        std::ofstream(root / "docs" / "note.txt") << "hello";
        std::ofstream(root / "secret.TUX") << "protected";
        std::ofstream(root / "audit.tlog") << "log";
    }

    FilesystemFileStore store(root.string());
    const auto entries = store.listDirectory("");
    if (!expect(!entries.empty(), "root entries should not be empty")) return false;
    if (!expect(store.readFile("docs/note.txt") == "hello", "read text mismatch")) return false;

    store.writeFile("docs/new.txt", "created");
    if (!expect(store.readFile("docs/new.txt") == "created", "write text mismatch")) return false;

    try {
        store.readFile("../user_data.dat");
        return expect(false, "path traversal should fail");
    } catch (const BackendException& error) {
        if (!expect(error.code() == ErrorCode::InvalidPath || error.code() == ErrorCode::PermissionDenied, "path traversal code mismatch")) return false;
    }

    try {
        store.readFile("secret.TUX");
        return expect(false, "TUX read should fail");
    } catch (const BackendException& error) {
        if (!expect(error.code() == ErrorCode::PermissionDenied, "TUX read code mismatch")) return false;
    }

    try {
        store.readFile("audit.tlog");
        return expect(false, "tlog read should fail");
    } catch (const BackendException& error) {
        if (!expect(error.code() == ErrorCode::PermissionDenied, "tlog read code mismatch")) return false;
    }

    fs::remove_all(root);
    return true;
}
```

Call it from `main()`:

```cpp
if (!runFilesystemStoreTests()) return 1;
```

- [ ] **Step 2: Run the failing adapter test**

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: build fails because `filesystem_file_store.hpp` does not exist.

- [ ] **Step 3: Implement the filesystem adapter**

Create `BACKEND/adapters/filesystem_file_store.hpp`:

```cpp
#pragma once

#include "file_store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tundraux::backend {

class FilesystemFileStore final : public FileStore {
public:
    explicit FilesystemFileStore(std::string root);

    std::vector<FileEntry> listDirectory(const std::string& path) override;
    std::string readFile(const std::string& path) override;
    void writeFile(const std::string& path, const std::string& content) override;

private:
    std::filesystem::path root_;

    std::filesystem::path resolveManagedPath(const std::string& path, bool allowRoot) const;
    void rejectProtectedPath(const std::filesystem::path& resolved) const;
};

} // namespace tundraux::backend
```

Create `BACKEND/adapters/filesystem_file_store.cpp` with these rules:

```cpp
#include "filesystem_file_store.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace tundraux::backend {
namespace {
constexpr std::uintmax_t kMaxTextContentBytes = 16 * 1024 * 1024;

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool hasDotPart(const std::filesystem::path& path) {
    for (const auto& part : path) {
        const auto text = part.u8string();
        if (text == "." || text == "..") return true;
    }
    return false;
}
} // namespace

FilesystemFileStore::FilesystemFileStore(std::string root)
    : root_(std::filesystem::absolute(std::filesystem::u8path(root)).lexically_normal()) {}

std::filesystem::path FilesystemFileStore::resolveManagedPath(const std::string& path, bool allowRoot) const {
    const std::filesystem::path relative = std::filesystem::u8path(path);
    if (relative.is_absolute() || hasDotPart(relative)) {
        throw BackendException(ErrorCode::InvalidPath, "Invalid path.");
    }
    if (path.empty() && !allowRoot) {
        throw BackendException(ErrorCode::InvalidPath, "Invalid path.");
    }
    const std::filesystem::path resolved = (root_ / relative).lexically_normal();
    const auto rootText = lowerCopy(root_.wstring().empty() ? root_.string() : root_.string());
    const auto resolvedText = lowerCopy(resolved.string());
    if (resolvedText.rfind(rootText, 0) != 0) {
        throw BackendException(ErrorCode::PermissionDenied, "Access denied.");
    }
    return resolved;
}

void FilesystemFileStore::rejectProtectedPath(const std::filesystem::path& resolved) const {
    const std::string ext = lowerCopy(resolved.extension().string());
    const std::string name = lowerCopy(resolved.filename().string());
    if (ext == ".tux" || ext == ".tlog" || name == "user_data.dat") {
        throw BackendException(ErrorCode::PermissionDenied, "Protected file.");
    }
}

std::vector<FileEntry> FilesystemFileStore::listDirectory(const std::string& path) {
    const auto resolved = resolveManagedPath(path, true);
    std::error_code error;
    if (!std::filesystem::exists(resolved, error)) {
        throw BackendException(ErrorCode::NotFound, "Directory not found.");
    }
    if (!std::filesystem::is_directory(resolved, error)) {
        throw BackendException(ErrorCode::InvalidPath, "Path is not a directory.");
    }
    std::vector<FileEntry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(resolved, error)) {
        if (error) break;
        if (entry.path().filename() == "temp") continue;
        FileEntry out;
        out.name = entry.path().filename().string();
        out.path = std::filesystem::relative(entry.path(), root_, error).generic_string();
        out.type = entry.is_directory(error) ? FileEntryType::Directory : FileEntryType::File;
        out.size = out.type == FileEntryType::File ? entry.file_size(error) : 0;
        entries.push_back(out);
    }
    std::sort(entries.begin(), entries.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
        if (lhs.type != rhs.type) return lhs.type == FileEntryType::Directory;
        return lowerCopy(lhs.name) < lowerCopy(rhs.name);
    });
    return entries;
}

std::string FilesystemFileStore::readFile(const std::string& path) {
    const auto resolved = resolveManagedPath(path, false);
    rejectProtectedPath(resolved);
    std::error_code error;
    if (!std::filesystem::exists(resolved, error)) {
        throw BackendException(ErrorCode::NotFound, "File not found.");
    }
    if (!std::filesystem::is_regular_file(resolved, error)) {
        throw BackendException(ErrorCode::InvalidPath, "Path is not a file.");
    }
    const auto size = std::filesystem::file_size(resolved, error);
    if (error || size > kMaxTextContentBytes) {
        throw BackendException(ErrorCode::StorageError, "File storage error.");
    }
    std::ifstream in(resolved, std::ios::binary);
    if (!in) throw BackendException(ErrorCode::StorageError, "File storage error.");
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

void FilesystemFileStore::writeFile(const std::string& path, const std::string& content) {
    if (content.size() > kMaxTextContentBytes) {
        throw BackendException(ErrorCode::StorageError, "File storage error.");
    }
    const auto resolved = resolveManagedPath(path, false);
    rejectProtectedPath(resolved);
    std::error_code error;
    std::filesystem::create_directories(resolved.parent_path(), error);
    if (error) throw BackendException(ErrorCode::StorageError, "File storage error.");
    std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
    if (!out) throw BackendException(ErrorCode::StorageError, "File storage error.");
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) throw BackendException(ErrorCode::StorageError, "File storage error.");
}

} // namespace tundraux::backend
```

Modify `CMakeLists.txt` so `tundraux_backend_adapters` includes `BACKEND/adapters/filesystem_file_store.cpp`.

Update `backend_file_service_tests` to include adapter headers and link `tundraux_backend_adapters`.

- [ ] **Step 4: Run adapter tests**

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_file_service_tests --output-on-failure
```

Expected: `backend_file_service_tests` passes.

- [ ] **Step 5: Commit**

```powershell
git add BACKEND/adapters/filesystem_file_store.hpp BACKEND/adapters/filesystem_file_store.cpp tests/backend_file_service_tests.cpp CMakeLists.txt
git commit -m "feat: add filesystem file store"
```

---

### Task 3: JSON-RPC File Methods

**Files:**
- Modify: `BACKEND/core/json_rpc.hpp`
- Modify: `BACKEND/core/json_rpc.cpp`
- Modify: `tests/backend_json_rpc_tests.cpp`

- [ ] **Step 1: Add JSON-RPC file method tests**

Extend `tests/backend_json_rpc_tests.cpp` with a fake `FileStore`, construct `FileService files(fileStore, sessions)`, and construct the dispatcher with files:

```cpp
class FakeFileStore final : public tundraux::backend::FileStore {
public:
    std::vector<tundraux::backend::FileEntry> listDirectory(const std::string&) override {
        return {{"note.txt", "note.txt", tundraux::backend::FileEntryType::File, 5}};
    }
    std::string readFile(const std::string& path) override {
        if (path == "missing.txt") {
            throw tundraux::backend::BackendException(tundraux::backend::ErrorCode::NotFound, "File not found.");
        }
        return "hello";
    }
    void writeFile(const std::string&, const std::string&) override {}
};
```

Add assertions after admin or user login:

```cpp
const std::string listFilesResponse = dispatcher.handleLine(
    R"({"id":"20","method":"file.listDirectory","params":{"sessionId":")" + sessionId + R"(","path":""}})"
);
const auto listFiles = parseJson(listFilesResponse);
if (!expect(listFiles.ok, "file list response should parse")) return false;
const auto& entries = listFiles.value.asObject().at("result").asObject().at("entries").asArray();
if (!expect(entries.size() == 1, "file list entry count mismatch")) return false;
if (!expect(entries[0].asObject().at("type").asString() == "file", "file type mismatch")) return false;

const std::string readFileResponse = dispatcher.handleLine(
    R"({"id":"21","method":"file.readFile","params":{"sessionId":")" + sessionId + R"(","path":"note.txt"}})"
);
const auto readFile = parseJson(readFileResponse);
if (!expect(readFile.value.asObject().at("result").asObject().at("content").asString() == "hello", "file content mismatch")) return false;

const std::string writeFileResponse = dispatcher.handleLine(
    R"({"id":"22","method":"file.writeFile","params":{"sessionId":")" + sessionId + R"(","path":"note.txt","content":"updated"}})"
);
const auto writeFile = parseJson(writeFileResponse);
if (!expect(writeFile.value.asObject().at("result").asObject().at("ok").asBool(), "file write ok mismatch")) return false;
```

- [ ] **Step 2: Run the failing JSON-RPC test**

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_json_rpc_tests --output-on-failure
```

Expected: build fails because `JsonRpcDispatcher` has no `FileService` constructor parameter or file methods.

- [ ] **Step 3: Update the dispatcher**

Modify `BACKEND/core/json_rpc.hpp`:

```cpp
#include "file_service.hpp"

class JsonRpcDispatcher {
public:
    JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files);
private:
    SessionService& sessions_;
    UserService& users_;
    FileService& files_;
};
```

Modify `BACKEND/core/json_rpc.cpp`:

```cpp
JsonRpcDispatcher::JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files)
    : sessions_(sessions), users_(users), files_(files) {}

JsonValue fileEntryToJson(const FileEntry& entry) {
    return JsonValue::object({
        {"name", JsonValue::string(entry.name)},
        {"path", JsonValue::string(entry.path)},
        {"type", JsonValue::string(entry.type == FileEntryType::Directory ? "directory" : "file")},
        {"size", JsonValue::number(static_cast<double>(entry.size))}
    });
}
```

Add dispatch branches:

```cpp
if (method == "file.listDirectory") {
    const auto result = files_.listDirectory(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "path")
    );
    if (!result.ok) throwIfFailed(result.error);
    JsonValue::Array entries;
    for (const auto& entry : result.value) entries.push_back(fileEntryToJson(entry));
    return JsonValue::object({{"entries", JsonValue::array(std::move(entries))}});
}

if (method == "file.readFile") {
    const auto result = files_.readFile(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "path")
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"content", JsonValue::string(result.value.content)}});
}

if (method == "file.writeFile") {
    const auto result = files_.writeFile(
        requiredStringParam(params, "sessionId"),
        requiredStringParam(params, "path"),
        requiredStringParam(params, "content")
    );
    if (!result.ok) throwIfFailed(result.error);
    return JsonValue::object({{"ok", JsonValue::boolean(true)}});
}
```

Update all existing `JsonRpcDispatcher dispatcher(sessions, users);` call sites in tests to pass a `FileService`.

- [ ] **Step 4: Run JSON-RPC tests**

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_json_rpc_tests --output-on-failure
```

Expected: `backend_json_rpc_tests` passes.

- [ ] **Step 5: Commit**

```powershell
git add BACKEND/core/json_rpc.hpp BACKEND/core/json_rpc.cpp tests/backend_json_rpc_tests.cpp
git commit -m "feat: expose file methods over json rpc"
```

---

### Task 4: Stdio Backend File Flow

**Files:**
- Modify: `BACKEND/stdio/main.cpp`
- Modify: `tests/backend_stdio_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add stdio process file API test**

Extend `tests/backend_stdio_tests.cpp` with an interactive process helper and a file API test that creates temp user data and temp files root, logs in, writes, reads, and lists.

Add includes:

```cpp
#include "crypto.hpp"
#include <windows.h>
```

Add a version-21 user-data writer:

```cpp
void writeRawString(std::ofstream& out, const std::string& value) {
    const std::size_t length = value.size();
    out.write(reinterpret_cast<const char*>(&length), sizeof(length));
    out.write(value.data(), static_cast<std::streamsize>(length));
}

void writeSingleUserData(
    const std::filesystem::path& path,
    const std::string& type,
    const std::string& username,
    const std::string& password
) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const int version = 21;
    const std::uint8_t strictValue = 0;
    const std::size_t userCount = 1;
    const int failedCount = 0;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&strictValue), sizeof(strictValue));
    out.write(reinterpret_cast<const char*>(&userCount), sizeof(userCount));
    writeRawString(out, type);
    writeRawString(out, username);
    writeRawString(out, encrypt(password));
    writeRawString(out, "");
    out.write(reinterpret_cast<const char*>(&failedCount), sizeof(failedCount));
}
```

Add an interactive stdio process fixture:

```cpp
class InteractiveBackendProcess {
public:
    bool start(const std::filesystem::path& backendExePath, const std::string& arguments) {
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE childStdoutWrite = INVALID_HANDLE_VALUE;
        HANDLE childStdinRead = INVALID_HANDLE_VALUE;
        if (!CreatePipe(&stdoutRead_, &childStdoutWrite, &security, 0)) return false;
        if (!SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0)) return false;
        if (!CreatePipe(&childStdinRead, &stdinWrite_, &security, 0)) return false;
        if (!SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0)) return false;

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = childStdinRead;
        startup.hStdOutput = childStdoutWrite;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        std::string commandLine = quotePath(backendExePath) + " " + arguments;
        const BOOL created = CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process_
        );
        CloseHandle(childStdoutWrite);
        CloseHandle(childStdinRead);
        running_ = created == TRUE;
        return running_;
    }

    bool request(const std::string& line, std::string& response) {
        const std::string payload = line + "\n";
        DWORD written = 0;
        if (!WriteFile(stdinWrite_, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)) return false;

        response.clear();
        char ch = '\0';
        DWORD read = 0;
        while (ReadFile(stdoutRead_, &ch, 1, &read, nullptr) && read == 1) {
            if (ch == '\n') break;
            if (ch != '\r') response.push_back(ch);
        }
        return !response.empty();
    }

    void stop() {
        if (stdinWrite_ != INVALID_HANDLE_VALUE) {
            CloseHandle(stdinWrite_);
            stdinWrite_ = INVALID_HANDLE_VALUE;
        }
        if (running_) {
            WaitForSingleObject(process_.hProcess, 3000);
            CloseHandle(process_.hThread);
            CloseHandle(process_.hProcess);
            running_ = false;
        }
        if (stdoutRead_ != INVALID_HANDLE_VALUE) {
            CloseHandle(stdoutRead_);
            stdoutRead_ = INVALID_HANDLE_VALUE;
        }
    }

    ~InteractiveBackendProcess() { stop(); }

private:
    HANDLE stdinWrite_ = INVALID_HANDLE_VALUE;
    HANDLE stdoutRead_ = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process_{};
    bool running_ = false;
};
```

Add the concrete file API process test:

```cpp
bool runFileApiProcessTest(const std::filesystem::path& backendExePath) {
    const auto base = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_process_file_api";
    const auto userDataPath = base.string() + ".dat";
    const auto filesRoot = base.string() + "_files";

    writeSingleUserData(userDataPath, "user", "alice", "Secret1");
    std::filesystem::remove_all(filesRoot);
    std::filesystem::create_directories(filesRoot);

    InteractiveBackendProcess process;
    if (!process.start(
            backendExePath,
            "--user-data " + quotePath(userDataPath) + " --files-root " + quotePath(filesRoot))) {
        std::cerr << "file API process did not start\n";
        std::filesystem::remove(userDataPath);
        std::filesystem::remove_all(filesRoot);
        return false;
    }

    std::string response;
    if (!process.request(R"({"id":"1","method":"session.startGuestSession","params":{}})", response)) return false;
    const auto started = tundraux::backend::parseJson(response);
    if (!started.ok) return false;
    const std::string sessionId = started.value.asObject().at("result").asObject().at("sessionId").asString();

    const std::string loginRequest =
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Secret1"}})";
    if (!process.request(loginRequest, response) || response.find(R"("type":"user")") == std::string::npos) return false;

    const std::string writeRequest =
        R"({"id":"3","method":"file.writeFile","params":{"sessionId":")" + sessionId +
        R"(","path":"note.txt","content":"hello"}})";
    if (!process.request(writeRequest, response) || response.find(R"("ok":true)") == std::string::npos) return false;

    const std::string readRequest =
        R"({"id":"4","method":"file.readFile","params":{"sessionId":")" + sessionId +
        R"(","path":"note.txt"}})";
    if (!process.request(readRequest, response) || response.find(R"("content":"hello")") == std::string::npos) return false;

    const std::string listRequest =
        R"({"id":"5","method":"file.listDirectory","params":{"sessionId":")" + sessionId +
        R"(","path":""}})";
    if (!process.request(listRequest, response) || response.find(R"("name":"note.txt")") == std::string::npos) return false;

    process.stop();
    std::filesystem::remove(userDataPath);
    std::filesystem::remove_all(filesRoot);
    return true;
}
```

Call it from `runProcessTests`:

```cpp
return runGuestSessionProcessTest(backendExePath) &&
    runInvalidCliProcessTest(backendExePath, "--bad", "unknown_arg") &&
    runInvalidCliProcessTest(backendExePath, "--user-data", "missing_user_data_value") &&
    runMalformedStorageProcessTest(backendExePath) &&
    runFileApiProcessTest(backendExePath);
```

- [ ] **Step 2: Run the failing stdio test**

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_stdio_tests --output-on-failure
```

Expected: build fails because stdio main has no `FileService` and no `--files-root`.

- [ ] **Step 3: Wire file service into stdio**

Modify `BACKEND/stdio/main.cpp`:

```cpp
#include "filesystem_file_store.hpp"
#include "file_service.hpp"

bool parseArgs(int argc, char* argv[], std::string& userDataPath, std::string& filesRoot) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--user-data") {
            if (i + 1 >= argc) { std::cerr << "--user-data requires a path.\n"; printUsage(); return false; }
            userDataPath = argv[++i];
        } else if (arg == "--files-root") {
            if (i + 1 >= argc) { std::cerr << "--files-root requires a path.\n"; printUsage(); return false; }
            filesRoot = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage();
            return false;
        }
    }
    return !userDataPath.empty() && !filesRoot.empty();
}
```

Update `main` construction:

```cpp
std::string userDataPath = "user_data.dat";
std::string filesRoot = "Files";
if (!parseArgs(argc, argv, userDataPath, filesRoot)) return 1;

tundraux::backend::DataManagerUserStore usersStore(userDataPath);
tundraux::backend::FilesystemFileStore filesStore(filesRoot);
tundraux::backend::SessionService sessions(usersStore);
tundraux::backend::UserService users(usersStore, sessions);
tundraux::backend::FileService files(filesStore, sessions);
tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users, files);
```

Update usage:

```cpp
std::cerr << "Usage: tundraux_backend_stdio [--user-data PATH] [--files-root PATH]\n";
```

- [ ] **Step 4: Run stdio tests**

Run:

```powershell
cmake --build build
ctest --test-dir build -R backend_stdio_tests --output-on-failure
```

Expected: `backend_stdio_tests` passes.

- [ ] **Step 5: Commit**

```powershell
git add BACKEND/stdio/main.cpp tests/backend_stdio_tests.cpp CMakeLists.txt
git commit -m "feat: wire file api into stdio backend"
```

---

### Task 5: Frontend Backend Client Unit

**Files:**
- Create: `APP/backend_client/backend_client.hpp`
- Create: `APP/backend_client/backend_client.cpp`
- Test: `tests/frontend_backend_client_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write frontend client tests**

Create `tests/frontend_backend_client_tests.cpp` with a fake line transport:

```cpp
#include "backend_client.hpp"

#include <iostream>
#include <queue>
#include <string>

using namespace tundraux::frontend;

namespace {
class FakeTransport final : public BackendLineTransport {
public:
    std::queue<std::string> responses;
    std::string lastLine;

    bool requestLine(const std::string& line, std::string& response) override {
        lastLine = line;
        if (responses.empty()) return false;
        response = responses.front();
        responses.pop();
        return true;
    }
};

bool expect(bool value, const std::string& message) {
    if (!value) std::cerr << message << "\n";
    return value;
}
} // namespace

int main() {
    FakeTransport transport;
    BackendClient client(transport);

    transport.responses.push(R"({"id":"1","result":{"sessionId":"abc","user":{"name":"","type":"guest"}}})");
    const auto session = client.startGuestSession();
    if (!expect(session.ok && session.value.sessionId == "abc", "start session failed")) return 1;
    if (!expect(transport.lastLine.find("session.startGuestSession") != std::string::npos, "wrong method")) return 1;

    transport.responses.push(R"({"id":"2","error":{"code":"PermissionDenied","message":"Access denied."}})");
    const auto users = client.listUsers("abc");
    if (!expect(!users.ok && users.errorCode == "PermissionDenied", "error response mismatch")) return 1;

    transport.responses.push(R"({"id":"3","result":{"content":"hello"}})");
    const auto content = client.readFile("abc", "note.txt");
    if (!expect(content.ok && content.value == "hello", "read file mismatch")) return 1;

    transport.responses.push("not-json");
    const auto malformed = client.whoami("abc");
    if (!expect(!malformed.ok, "malformed response should fail")) return 1;

    return 0;
}
```

- [ ] **Step 2: Register and run failing frontend client test**

Add:

```cmake
add_executable(frontend_backend_client_tests
    tests/frontend_backend_client_tests.cpp
    APP/backend_client/backend_client.cpp
)

target_include_directories(frontend_backend_client_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/APP/backend_client
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(frontend_backend_client_tests
    PRIVATE
        tundraux_backend_core
)

target_compile_features(frontend_backend_client_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME frontend_backend_client_tests COMMAND frontend_backend_client_tests)
```

Run:

```powershell
cmake --build build
ctest --test-dir build -R frontend_backend_client_tests --output-on-failure
```

Expected: build fails because `backend_client.hpp` does not exist.

- [ ] **Step 3: Implement typed client and transport interface**

Create `APP/backend_client/backend_client.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

namespace tundraux::frontend {

struct FrontendUser {
    std::string name;
    std::string type;
};

struct FrontendSession {
    std::string sessionId;
    FrontendUser user;
};

struct FrontendFileEntry {
    std::string name;
    std::string path;
    std::string type;
    unsigned long long size = 0;
};

template <typename T>
struct ClientResult {
    bool ok = false;
    T value{};
    std::string errorCode;
    std::string message;
};

class BackendLineTransport {
public:
    virtual ~BackendLineTransport() = default;
    virtual bool requestLine(const std::string& line, std::string& response) = 0;
};

class BackendClient {
public:
    explicit BackendClient(BackendLineTransport& transport);

    ClientResult<FrontendSession> startGuestSession();
    ClientResult<FrontendSession> login(const std::string& sessionId, const std::string& username, const std::string& password);
    ClientResult<bool> logout(const std::string& sessionId);
    ClientResult<FrontendUser> whoami(const std::string& sessionId);
    ClientResult<std::vector<FrontendUser>> listUsers(const std::string& sessionId);
    ClientResult<std::vector<FrontendFileEntry>> listDirectory(const std::string& sessionId, const std::string& path);
    ClientResult<std::string> readFile(const std::string& sessionId, const std::string& path);
    ClientResult<bool> writeFile(const std::string& sessionId, const std::string& path, const std::string& content);

private:
    BackendLineTransport& transport_;
    int nextId_ = 1;
};

} // namespace tundraux::frontend
```

Create `APP/backend_client/backend_client.cpp` using `parseJson`, `stringifyJson`, and `JsonValue` from `BACKEND/core/json.hpp`. Use helper functions to:

- assign string ids from `nextId_`
- build params as `JsonValue::object`
- parse `{ "error": { "code", "message" } }`
- parse result payloads into frontend DTOs
- return `ClientResult<T>{false, {}, "TransportError", "Backend unavailable."}` when transport fails
- return `ClientResult<T>{false, {}, "InvalidResponse", "Invalid backend response."}` when response parsing or expected fields fail

- [ ] **Step 4: Run frontend client tests**

Run:

```powershell
cmake --build build
ctest --test-dir build -R frontend_backend_client_tests --output-on-failure
```

Expected: `frontend_backend_client_tests` passes.

- [ ] **Step 5: Commit**

```powershell
git add APP/backend_client/backend_client.hpp APP/backend_client/backend_client.cpp tests/frontend_backend_client_tests.cpp CMakeLists.txt
git commit -m "feat: add frontend backend rpc client"
```

---

### Task 6: Frontend Backend Process Runtime

**Files:**
- Create: `APP/backend_client/backend_process.hpp`
- Create: `APP/backend_client/backend_process.cpp`
- Create: `APP/backend_client/backend_runtime.hpp`
- Create: `APP/backend_client/backend_runtime.cpp`
- Modify: `CORE/main/main.cpp`
- Modify: `APP/shell/command.hpp`
- Modify: `APP/shell/command.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add client startup failure test**

Extend `tests/frontend_backend_client_tests.cpp` with a fake transport that returns `false` and assert `TransportError`:

```cpp
class FailingTransport final : public BackendLineTransport {
public:
    bool requestLine(const std::string&, std::string&) override { return false; }
};

FailingTransport failing;
BackendClient failingClient(failing);
const auto failed = failingClient.startGuestSession();
if (!expect(!failed.ok && failed.errorCode == "TransportError", "transport failure mismatch")) return 1;
```

Run:

```powershell
cmake --build build
ctest --test-dir build -R frontend_backend_client_tests --output-on-failure
```

Expected: passes if Task 5 implemented transport errors correctly.

- [ ] **Step 2: Implement process transport**

Create `APP/backend_client/backend_process.hpp`:

```cpp
#pragma once

#include "backend_client.hpp"

#include <string>
#include <windows.h>

namespace tundraux::frontend {

class BackendProcessTransport final : public BackendLineTransport {
public:
    BackendProcessTransport();
    ~BackendProcessTransport() override;

    BackendProcessTransport(const BackendProcessTransport&) = delete;
    BackendProcessTransport& operator=(const BackendProcessTransport&) = delete;

    bool start(const std::string& executablePath, const std::string& userDataPath, const std::string& filesRoot);
    bool requestLine(const std::string& line, std::string& response) override;
    void stop();

private:
    HANDLE childStdinWrite_ = INVALID_HANDLE_VALUE;
    HANDLE childStdoutRead_ = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION processInfo_{};
    bool running_ = false;
};

} // namespace tundraux::frontend
```

Create `APP/backend_client/backend_process.cpp` using `CreatePipe`, `SetHandleInformation`, `CreateProcessA`, `WriteFile`, and `ReadFile`. The request method must write `line + "\n"` and read until one newline from backend stdout. It must not merge stderr into stdout.

Create `APP/backend_client/backend_runtime.hpp`:

```cpp
#pragma once

#include "backend_client.hpp"
#include "backend_process.hpp"

#include <memory>
#include <string>

namespace tundraux::frontend {

struct BackendRuntimeOptions {
    bool legacyDirect = false;
    std::string backendStdioPath;
    std::string userDataPath = "user_data.dat";
    std::string filesRoot = "Files";
};

class BackendRuntime {
public:
    bool initialize(const BackendRuntimeOptions& options, std::string& error);
    BackendClient* client();
    const std::string& sessionId() const;
    void setSessionId(std::string sessionId);
    bool legacyDirect() const;
    void shutdown();

private:
    bool legacyDirect_ = false;
    std::unique_ptr<BackendProcessTransport> transport_;
    std::unique_ptr<BackendClient> client_;
    std::string sessionId_;
};

} // namespace tundraux::frontend
```

Create `APP/backend_client/backend_runtime.cpp` to:

- return true without client when `legacyDirect` is true
- start `BackendProcessTransport`
- create `BackendClient`
- call `startGuestSession`
- store the session id

- [ ] **Step 3: Parse runtime args and initialize before shell**

Modify `CORE/main/main.cpp`:

```cpp
#include "backend_runtime.hpp"

tundraux::frontend::BackendRuntimeOptions parseRuntimeOptions(int argc, char* argv[]) {
    tundraux::frontend::BackendRuntimeOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--legacy-direct") {
            options.legacyDirect = true;
        } else if (arg == "--backend-stdio" && i + 1 < argc) {
            options.backendStdioPath = argv[++i];
        }
    }
    return options;
}
```

Pass runtime into shell:

```cpp
tundraux::frontend::BackendRuntime backendRuntime;
std::string backendError;
if (!backendRuntime.initialize(parseRuntimeOptions(argc, argv), backendError)) {
    colorcout("red", backendError + "\n");
    pause();
    return 1;
}
task_main(&backendRuntime);
backendRuntime.shutdown();
```

Modify `APP/shell/command.hpp`:

```cpp
namespace tundraux::frontend { class BackendRuntime; }
void task_main(tundraux::frontend::BackendRuntime* backendRuntime = nullptr);
```

Modify `APP/shell/command.cpp` signature and keep behavior unchanged for now.

- [ ] **Step 4: Build**

Run:

```powershell
cmake --build build
ctest --test-dir build -R frontend_backend_client_tests --output-on-failure
```

Expected: build succeeds and frontend client tests still pass.

- [ ] **Step 5: Commit**

```powershell
git add APP/backend_client/backend_process.hpp APP/backend_client/backend_process.cpp APP/backend_client/backend_runtime.hpp APP/backend_client/backend_runtime.cpp CORE/main/main.cpp APP/shell/command.hpp APP/shell/command.cpp CMakeLists.txt
git commit -m "feat: start local stdio backend from frontend"
```

---

### Task 7: Shell Command Migration

**Files:**
- Modify: `APP/shell/commandHandlers.hpp`
- Modify: `APP/shell/commandHandlers.cpp`
- Modify: `APP/shell/commandReg.cpp`
- Modify: `APP/shell/command.cpp`

- [ ] **Step 1: Add handler context type**

Modify `APP/shell/commandHandlers.hpp`:

```cpp
namespace tundraux::frontend { class BackendRuntime; }

struct ShellContext {
    USER& currentUser;
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr;
};

void handleLoginCommand(const std::string& input, ShellContext& context);
void handleLogoutCommand(const std::string& input, ShellContext& context);
void handleListUserCommand(const std::string& input, ShellContext& context);
void handleWhoamiCommand(ShellContext& context);
```

Keep legacy overloads only where needed internally:

```cpp
void handleLoginCommandLegacy(const std::string& input, USER& currentUser);
```

- [ ] **Step 2: Implement backend-backed handlers**

In `APP/shell/commandHandlers.cpp`, convert frontend users to legacy snapshots:

```cpp
USER toLegacySnapshot(const tundraux::frontend::FrontendUser& user) {
    return USER{user.type, user.name, "", "", 0};
}
```

In `handleLoginCommand`, if no runtime or legacy direct, call legacy implementation. Otherwise:

```cpp
auto* client = context.backendRuntime->client();
const std::string password = getHiddenInput("Please enter password for user " + username + ": ", '*');
const auto result = client->login(context.backendRuntime->sessionId(), username, password);
if (!result.ok) {
    tundraux::audit::logEvent("login", "failure " + username);
    colorcout("red", result.message.empty() ? "Authentication failed.\n" : result.message + "\n");
    return;
}
context.backendRuntime->setSessionId(result.value.sessionId);
context.currentUser = toLegacySnapshot(result.value.user);
tundraux::audit::setCurrentUser(context.currentUser);
tundraux::audit::logEvent("login", "success " + username);
rollcout("green", "Welcome, " + context.currentUser.name + "!");
```

In `logout`, call `client->logout(sessionId)`, then set current user to guest on success.

In `listuser`, call `client->listUsers(sessionId)` and print `name (type)` for each user. Never print password or hint.

In `whoami`, call `client->whoami(sessionId)` and refresh the snapshot before printing.

- [ ] **Step 3: Capture context in command registry**

Change `buildNewCommandRegistry` signature in `APP/shell/commandReg.hpp` and `.cpp`:

```cpp
std::vector<RegisteredCommand> buildNewCommandRegistry(ShellContext& context);
```

Update command lambdas:

```cpp
[&context](const std::string& input) { handleLoginCommand(input, context); }
[&context](const std::string& input) { handleLogoutCommand(input, context); }
[&context](const std::string& input) { handleListUserCommand(input, context); }
[&context](const std::string&) { handleWhoamiCommand(context); }
```

Modify `task_main` to create:

```cpp
ShellContext shellContext{currentUser, backendRuntime};
std::vector<RegisteredCommand> registeredCommands = buildNewCommandRegistry(shellContext);
```

- [ ] **Step 4: Build and run command-related tests**

Run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```powershell
git add APP/shell/commandHandlers.hpp APP/shell/commandHandlers.cpp APP/shell/commandReg.hpp APP/shell/commandReg.cpp APP/shell/command.cpp
git commit -m "feat: route shell session commands through backend"
```

---

### Task 8: Plain Editor Backend File Adapter

**Files:**
- Modify: `APP/shell/commandHandlers.cpp`
- Modify: `APP/shell/commandHandlers.hpp` if helper declarations are needed

- [ ] **Step 1: Add helper for backend-managed edit flow**

In `APP/shell/commandHandlers.cpp`, add:

```cpp
bool editManagedFileViaBackend(
    tundraux::frontend::BackendRuntime& runtime,
    const std::string& requestedPath,
    const std::string& displayName
) {
    auto* client = runtime.client();
    const auto read = client->readFile(runtime.sessionId(), requestedPath);
    std::string content;
    if (read.ok) {
        content = read.value;
    } else if (read.errorCode == "NotFound") {
        content = "";
    } else {
        colorcout("red", read.message.empty() ? "Failed to read file.\n" : read.message + "\n");
        return false;
    }

    const fs::path tempRoot = fs::current_path() / "Files" / "temp";
    std::error_code ec;
    fs::create_directories(tempRoot, ec);
    if (ec) {
        colorcout("red", "Failed to create editor temp directory.\n");
        return false;
    }

    const fs::path tempPath = tempRoot / ("backend-edit-" + std::to_string(std::time(nullptr)) + ".txt");
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        out << content;
    }

    const int editorResult = run_editor(tempPath.string(), displayName);
    if (editorResult == 0) {
        std::ifstream in(tempPath, std::ios::binary);
        std::ostringstream updated;
        updated << in.rdbuf();
        const auto write = client->writeFile(runtime.sessionId(), requestedPath, updated.str());
        if (!write.ok) {
            colorcout("red", write.message.empty() ? "Failed to save file.\n" : write.message + "\n");
            fs::remove(tempPath, ec);
            return false;
        }
    }
    fs::remove(tempPath, ec);
    return editorResult == 0;
}
```

Add required includes:

```cpp
#include "backend_runtime.hpp"
#include <fstream>
```

- [ ] **Step 2: Route `edit` through backend when runtime is active**

Change `handleEditCommand` to accept `ShellContext&`. Preserve existing validation, `.TUX` denial, `.tlog` denial, and path checks. When backend runtime exists and is not legacy direct:

```cpp
if (context.backendRuntime != nullptr && !context.backendRuntime->legacyDirect()) {
    editManagedFileViaBackend(*context.backendRuntime, requestedPath.generic_string(), filename);
    return;
}
```

Keep existing local path behavior for `--legacy-direct`.

- [ ] **Step 3: Build**

Run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```powershell
git add APP/shell/commandHandlers.cpp APP/shell/commandHandlers.hpp APP/shell/commandReg.cpp
git commit -m "feat: route plain editor files through backend"
```

---

### Task 9: Documentation And Full Verification

**Files:**
- Modify: `readme.md`
- Modify: `README.zh-CN.md`

- [ ] **Step 1: Update English README**

Change the backend section in `readme.md` to state:

```markdown
### Backend Phase 2

`TundraUX2` now starts a local `tundraux_backend_stdio` process in the default runtime mode. The frontend uses line-delimited JSON-RPC for session startup, login, logout, whoami, user listing, and basic managed-file list/read/write operations under `Files`.

Use `--legacy-direct` to bypass the backend client for local regression comparison. Use `--backend-stdio <path>` to point the frontend at a specific backend executable.

TUX import/export/metadata, full Explorer mutation migration, audit APIs, HTTP transport, remote access, and shared daemon mode remain future work.
```

- [ ] **Step 2: Update Chinese README**

Update `README.zh-CN.md` with equivalent content. If the existing file encoding renders incorrectly in the terminal, edit only the backend section and preserve the file's current encoding as much as possible.

- [ ] **Step 3: Run full validation**

Run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: build succeeds and all tests pass.

- [ ] **Step 4: Manual smoke validation**

Run:

```powershell
.\build\Debug\TundraUX2.exe --backend-stdio .\build\Debug\tundraux_backend_stdio.exe
```

Manual checks:

- `whoami` returns guest state.
- `login <user>` authenticates through backend.
- `listuser` succeeds for admin/debug and does not print passwords.
- `edit notes.txt` opens a plain managed file through backend read/write.
- `TUXfile` still opens through legacy implementation.

- [ ] **Step 5: Commit**

```powershell
git add readme.md README.zh-CN.md
git commit -m "docs: describe backend phase two runtime"
```

---

## Final Completion

- [ ] **Step 1: Run full status and test check**

```powershell
git status --short
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected:

- no unexpected unstaged source changes
- build succeeds
- all tests pass

- [ ] **Step 2: Summarize final scope**

Final response must explicitly state:

- backend stdio is now the default local runtime boundary
- migrated shell commands: `login`, `logout`, `whoami`, `listuser`
- backend file APIs: `file.listDirectory`, `file.readFile`, `file.writeFile`
- still legacy: full TUX advanced commands, audit API backend migration, HTTP/remote/shared daemon

#include "backend_error.hpp"
#include "filesystem_file_store.hpp"
#include "file_service.hpp"
#include "file_store.hpp"
#include "session_service.hpp"
#include "user_store.hpp"

#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

class InMemoryUserStore final : public tundraux::backend::UserStore {
public:
    std::vector<tundraux::backend::BackendUser> users{
        {"user", "alice", "Secret1", "hint", 0}
    };

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
};

class InMemoryFileStore final : public tundraux::backend::FileStore {
public:
    std::string writtenPath;
    std::string writtenContent;

    std::vector<tundraux::backend::FileEntry> listDirectory(const std::string& path) const override {
        if (path != "") {
            return {};
        }
        return {
            {"note.txt", "note.txt", tundraux::backend::FileEntryType::File, 5}
        };
    }

    tundraux::backend::FileContent readFile(const std::string& path) const override {
        if (path == "secret.TUX") {
            throw tundraux::backend::BackendException(tundraux::backend::ErrorCode::PermissionDenied, "Access denied.");
        }
        if (path == "missing.txt") {
            throw tundraux::backend::BackendException(tundraux::backend::ErrorCode::NotFound, "File not found.");
        }
        return {"hello"};
    }

    void writeFile(const std::string& path, const std::string& content) override {
        writtenPath = path;
        writtenContent = content;
    }
};

class TempDirectory final {
public:
    explicit TempDirectory(std::filesystem::path root) : root_(std::move(root)) {
        std::filesystem::remove_all(root_);
    }

    ~TempDirectory() {
        std::filesystem::remove_all(root_);
    }

    const std::filesystem::path& path() const {
        return root_;
    }

private:
    std::filesystem::path root_;
};

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool expectBackendException(
    const std::string& message,
    tundraux::backend::ErrorCode expected,
    const std::function<void()>& action
) {
    try {
        action();
    } catch (const tundraux::backend::BackendException& error) {
        return expect(error.code() == expected, message);
    }
    return expect(false, message);
}

bool expectBackendExceptionOneOf(
    const std::string& message,
    tundraux::backend::ErrorCode first,
    tundraux::backend::ErrorCode second,
    const std::function<void()>& action
) {
    try {
        action();
    } catch (const tundraux::backend::BackendException& error) {
        return expect(error.code() == first || error.code() == second, message);
    }
    return expect(false, message);
}

bool expectEntry(
    const tundraux::backend::FileEntry& entry,
    const std::string& name,
    const std::string& path,
    tundraux::backend::FileEntryType type
) {
    return expect(entry.name == name, "filesystem listDirectory name mismatch") &&
        expect(entry.path == path, "filesystem listDirectory path mismatch") &&
        expect(entry.type == type, "filesystem listDirectory type mismatch");
}

bool hasEntryNamed(const std::vector<tundraux::backend::FileEntry>& entries, const std::string& name) {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    using namespace tundraux::backend;

    InMemoryUserStore users;
    SessionService sessions(users);
    InMemoryFileStore files;
    FileService service(files, sessions);

    const auto guest = sessions.startGuestSession();
    const auto guestList = service.listDirectory(guest.sessionId, "");
    if (!expect(!guestList.ok, "guest listDirectory should fail")) return 1;
    if (!expect(guestList.error.code == ErrorCode::PermissionDenied, "guest listDirectory code mismatch")) return 1;
    if (!expect(guestList.error.message == "Access denied.", "guest listDirectory message mismatch")) return 1;

    const auto login = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "alice login should pass")) return 1;

    const auto list = service.listDirectory(guest.sessionId, "");
    if (!expect(list.ok, "authenticated listDirectory should pass")) return 1;
    if (!expect(list.value.size() == 1, "authenticated listDirectory count mismatch")) return 1;
    if (!expect(list.value[0].path == "note.txt", "authenticated listDirectory path mismatch")) return 1;

    const auto read = service.readFile(guest.sessionId, "note.txt");
    if (!expect(read.ok, "readFile should pass")) return 1;
    if (!expect(read.value.content == "hello", "readFile content mismatch")) return 1;

    const auto write = service.writeFile(guest.sessionId, "draft.txt", "updated");
    if (!expect(write.ok, "writeFile should pass")) return 1;
    if (!expect(files.writtenPath == "draft.txt", "writeFile path mismatch")) return 1;
    if (!expect(files.writtenContent == "updated", "writeFile content mismatch")) return 1;

    const auto denied = service.readFile(guest.sessionId, "secret.TUX");
    if (!expect(!denied.ok, "secret.TUX readFile should fail")) return 1;
    if (!expect(denied.error.code == ErrorCode::PermissionDenied, "secret.TUX error code mismatch")) return 1;
    if (!expect(denied.error.message == "Access denied.", "secret.TUX error message mismatch")) return 1;

    const auto missing = service.readFile(guest.sessionId, "missing.txt");
    if (!expect(!missing.ok, "missing.txt readFile should fail")) return 1;
    if (!expect(missing.error.code == ErrorCode::NotFound, "missing.txt error code mismatch")) return 1;
    if (!expect(missing.error.message == "File not found.", "missing.txt error message mismatch")) return 1;

    const auto root = std::filesystem::temp_directory_path() /
        std::filesystem::path("tundraux_backend_file_store_tests");
    TempDirectory tempRoot(root);
    std::filesystem::create_directories(tempRoot.path() / "docs");
    std::filesystem::create_directories(tempRoot.path() / "Alpha");
    std::filesystem::create_directories(tempRoot.path() / "beta");
    std::filesystem::create_directories(tempRoot.path() / "temp");
    std::ofstream(tempRoot.path() / "Zoo.txt") << "zoo";
    std::ofstream(tempRoot.path() / "apple.txt") << "apple";
    std::ofstream(tempRoot.path() / "docs" / "note.txt") << "hello";
    std::ofstream(tempRoot.path() / "secret.TUX") << "protected";
    std::ofstream(tempRoot.path() / "audit.tlog") << "protected";
    std::ofstream(tempRoot.path() / "USER_DATA.DAT") << "protected";

    FilesystemFileStore store(tempRoot.path().string());

    const auto entries = store.listDirectory("");
    if (!expect(entries.size() >= 5, "filesystem listDirectory root should return entries")) return 1;
    if (!expectEntry(entries[0], "Alpha", "Alpha", FileEntryType::Directory)) return 1;
    if (!expectEntry(entries[1], "beta", "beta", FileEntryType::Directory)) return 1;
    if (!expectEntry(entries[2], "docs", "docs", FileEntryType::Directory)) return 1;
    if (!expectEntry(entries[3], "apple.txt", "apple.txt", FileEntryType::File)) return 1;
    if (!expect(!hasEntryNamed(entries, "temp"), "filesystem listDirectory should skip temp entry")) return 1;

    const auto note = store.readFile("docs/note.txt");
    if (!expect(note.content == "hello", "filesystem readFile content mismatch")) return 1;

    store.writeFile("docs/new.txt", "created");
    const auto created = store.readFile("docs/new.txt");
    if (!expect(created.content == "created", "filesystem writeFile content mismatch")) return 1;

    store.writeFile("nested/child/new.txt", "nested");
    const auto nested = store.readFile("nested/child/new.txt");
    if (!expect(nested.content == "nested", "filesystem writeFile should create parent directories")) return 1;

    store.writeFile("docs/note.txt", "longer content");
    store.writeFile("docs/note.txt", "short");
    const auto truncated = store.readFile("docs/note.txt");
    if (!expect(truncated.content == "short", "filesystem writeFile should truncate existing content")) return 1;

    if (!expectBackendExceptionOneOf(
        "absolute root path should fail with InvalidPath or PermissionDenied",
        ErrorCode::InvalidPath,
        ErrorCode::PermissionDenied,
        [&store, &tempRoot]() { (void)store.readFile(tempRoot.path().string()); })) return 1;
    if (!expectBackendException(
        "docs/./note.txt should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("docs/./note.txt"); })) return 1;
    if (!expectBackendException(
        "docs/../note.txt should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("docs/../note.txt"); })) return 1;
    if (!expectBackendException(
        "empty readFile path should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile(""); })) return 1;
    if (!expectBackendException(
        "empty writeFile path should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("", "x"); })) return 1;
    if (!expectBackendException(
        "listDirectory missing path should fail with NotFound",
        ErrorCode::NotFound,
        [&store]() { (void)store.listDirectory("missing"); })) return 1;
    if (!expectBackendException(
        "listDirectory file path should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.listDirectory("docs/note.txt"); })) return 1;
    if (!expectBackendException(
        "missing readFile path should fail with NotFound",
        ErrorCode::NotFound,
        [&store]() { (void)store.readFile("missing.txt"); })) return 1;
    if (!expectBackendException(
        "directory readFile path should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("docs"); })) return 1;
    if (!expectBackendException(
        "oversize writeFile content should fail with StorageError",
        ErrorCode::StorageError,
        [&store]() { store.writeFile("oversize.txt", std::string(16u * 1024u * 1024u + 1u, 'x')); })) return 1;

    if (!expectBackendExceptionOneOf(
        "../user_data.dat should fail with InvalidPath or PermissionDenied",
        ErrorCode::InvalidPath,
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.readFile("../user_data.dat"); })) return 1;
    if (!expectBackendException(
        "secret.TUX should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.readFile("secret.TUX"); })) return 1;
    if (!expectBackendException(
        "audit.tlog should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.readFile("audit.tlog"); })) return 1;
    if (!expectBackendException(
        "secret.TuX should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.readFile("secret.TuX"); })) return 1;
    if (!expectBackendException(
        "audit.TLOG write should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { store.writeFile("audit.TLOG", "x"); })) return 1;
    if (!expectBackendException(
        "USER_DATA.DAT should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.readFile("USER_DATA.DAT"); })) return 1;
    std::filesystem::create_directories(tempRoot.path() / "protected.TLOG");
    if (!expectBackendException(
        "protected.TLOG listDirectory should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.listDirectory("protected.TLOG"); })) return 1;
    if (!expectBackendExceptionOneOf(
        "user_data.dat:stream write should fail with InvalidPath or PermissionDenied",
        ErrorCode::InvalidPath,
        ErrorCode::PermissionDenied,
        [&store]() { store.writeFile("user_data.dat:stream", "x"); })) return 1;
    if (!expectBackendException(
        "docs/new.txt:stream write should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("docs/new.txt:stream", "x"); })) return 1;

    return 0;
}

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
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <chrono>
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

    void deleteFile(const std::string&) override {}

    void renameFile(const std::string&, const std::string&, bool) override {}

    void copyFile(const std::string&, const std::string&, bool) override {}

    void moveFile(const std::string&, const std::string&, bool) override {}

    void createDirectory(const std::string&) override {}

    void removeDirectory(const std::string&, bool) override {}

    std::vector<tundraux::backend::FileEntry> search(const std::string&, const std::string&) const override {
        return {};
    }
};

class RecordingFileStore final : public tundraux::backend::FileStore {
public:
    std::vector<tundraux::backend::FileEntry> entries;
    std::string content;
    std::vector<std::string> calls;
    bool throwUnknownOnDelete = false;
    bool throwUnknownOnSearch = false;

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
        if (throwUnknownOnDelete) {
            throw 42;
        }
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
        if (throwUnknownOnSearch) {
            throw 42;
        }
        return entries;
    }
};

class TempDirectory final {
public:
    explicit TempDirectory(std::filesystem::path root) : root_(std::move(root)) {
        verifyUnderTempRoot();
        std::filesystem::remove_all(root_);
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
        ("tundraux_backend_file_store_tests_" + std::to_string(ticks) + "_" + std::to_string(threadId));
}

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

bool expectBackendException(
    const std::string& message,
    tundraux::backend::ErrorCode expectedCode,
    const std::string& expectedMessage,
    const std::function<void()>& action
) {
    try {
        action();
    } catch (const tundraux::backend::BackendException& error) {
        return expect(error.code() == expectedCode, message + " code mismatch") &&
            expect(error.what() == expectedMessage, message + " message mismatch");
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

bool createdSymlink(const std::filesystem::path& target, const std::filesystem::path& link) {
    std::error_code error;
    std::filesystem::create_symlink(target, link, error);
    if (error) {
        std::cerr << "Skipping file symlink assertions: " << error.message() << "\n";
        return false;
    }
    return true;
}

bool createdDirectorySymlink(const std::filesystem::path& target, const std::filesystem::path& link) {
    std::error_code error;
    std::filesystem::create_directory_symlink(target, link, error);
    if (error) {
        std::cerr << "Skipping directory symlink assertions: " << error.message() << "\n";
        return false;
    }
    return true;
}

bool regular_file_mutations_require_user_session() {
    RecordingFileStore store;
    InMemoryUserStore users;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FileService service(store, sessions);
    const auto guest = sessions.startGuestSession();

    const auto deleted = service.deleteFile(guest.sessionId, "a.txt");
    const auto renamed = service.renameFile(guest.sessionId, "a.txt", "b.txt", false);
    const auto copied = service.copyFile(guest.sessionId, "a.txt", "b.txt", true);
    const auto moved = service.moveFile(guest.sessionId, "a.txt", "docs/a.txt", false);
    const auto created = service.createDirectory(guest.sessionId, "docs");
    const auto removed = service.removeDirectory(guest.sessionId, "docs", false);
    const auto searched = service.search(guest.sessionId, "", "a");

    return expect(!deleted.ok, "guest delete should fail") &&
        expect(deleted.error.code == tundraux::backend::ErrorCode::PermissionDenied, "guest delete code mismatch") &&
        expect(!renamed.ok, "guest rename should fail") &&
        expect(renamed.error.code == tundraux::backend::ErrorCode::PermissionDenied, "guest rename code mismatch") &&
        expect(!copied.ok, "guest copy should fail") &&
        expect(copied.error.code == tundraux::backend::ErrorCode::PermissionDenied, "guest copy code mismatch") &&
        expect(!moved.ok, "guest move should fail") &&
        expect(moved.error.code == tundraux::backend::ErrorCode::PermissionDenied, "guest move code mismatch") &&
        expect(!created.ok, "guest mkdir should fail") &&
        expect(created.error.code == tundraux::backend::ErrorCode::PermissionDenied, "guest mkdir code mismatch") &&
        expect(!removed.ok, "guest rmdir should fail") &&
        expect(removed.error.code == tundraux::backend::ErrorCode::PermissionDenied, "guest rmdir code mismatch") &&
        expect(!searched.ok, "guest search should fail") &&
        expect(searched.error.code == tundraux::backend::ErrorCode::PermissionDenied, "guest search code mismatch") &&
        expect(store.calls.empty(), "guest calls should not reach store");
}

bool regular_file_mutations_delegate_for_logged_in_user() {
    RecordingFileStore store;
    InMemoryUserStore users;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FileService service(store, sessions);
    const auto guest = sessions.startGuestSession();
    const auto loggedIn = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(loggedIn.ok, "alice login should pass before file mutations")) return false;

    const bool ok =
        service.deleteFile(loggedIn.value.sessionId, "old.txt").ok &&
        service.renameFile(loggedIn.value.sessionId, "old.txt", "new.txt", false).ok &&
        service.copyFile(loggedIn.value.sessionId, "new.txt", "copy.txt", true).ok &&
        service.moveFile(loggedIn.value.sessionId, "copy.txt", "archive/copy.txt", false).ok &&
        service.createDirectory(loggedIn.value.sessionId, "archive").ok &&
        service.removeDirectory(loggedIn.value.sessionId, "archive", false).ok &&
        service.search(loggedIn.value.sessionId, "", "copy").ok;

    const std::vector<std::string> expectedCalls{
        "delete:old.txt",
        "rename:old.txt:new.txt:0",
        "copy:new.txt:copy.txt:1",
        "move:copy.txt:archive/copy.txt:0",
        "mkdir:archive",
        "rmdir:archive:0",
        "search::copy"
    };

    return expect(ok, "logged-in file mutations should succeed") &&
        expect(store.calls == expectedCalls, "logged-in delegated calls mismatch");
}

bool regular_file_operations_map_unknown_exceptions_to_storage_error() {
    RecordingFileStore store;
    store.throwUnknownOnDelete = true;
    store.throwUnknownOnSearch = true;
    InMemoryUserStore users;
    tundraux::backend::SessionService sessions(users);
    tundraux::backend::FileService service(store, sessions);
    const auto guest = sessions.startGuestSession();
    const auto loggedIn = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(loggedIn.ok, "alice login should pass before file error mapping")) return false;

    const auto deleted = service.deleteFile(loggedIn.value.sessionId, "old.txt");
    const auto searched = service.search(loggedIn.value.sessionId, "", "old");

    return expect(!deleted.ok, "unknown mutation exception should fail") &&
        expect(
            deleted.error.code == tundraux::backend::ErrorCode::StorageError,
            "unknown mutation exception code mismatch") &&
        expect(!searched.ok, "unknown search exception should fail") &&
        expect(
            searched.error.code == tundraux::backend::ErrorCode::StorageError,
            "unknown search exception code mismatch");
}

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

} // namespace

int main() {
    using namespace tundraux::backend;

    InMemoryUserStore users;
    SessionService sessions(users);
    InMemoryFileStore files;
    FileService service(files, sessions);

    if (!regular_file_mutations_require_user_session()) return 1;
    if (!regular_file_mutations_delegate_for_logged_in_user()) return 1;
    if (!regular_file_operations_map_unknown_exceptions_to_storage_error()) return 1;
    if (!filesystem_file_store_mutates_regular_files()) return 1;
    if (!filesystem_file_store_rejects_regular_file_conflicts()) return 1;
    if (!filesystem_file_store_searches_by_name()) return 1;

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

    TempDirectory tempRoot(uniqueTempPath());
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

    const auto docsEntries = store.listDirectory("docs");
    if (!expect(docsEntries.size() == 1, "filesystem listDirectory docs count mismatch")) return 1;
    if (!expectEntry(docsEntries[0], "note.txt", "docs/note.txt", FileEntryType::File)) return 1;

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
    const std::string limitContent(16u * 1024u * 1024u, 'a');
    store.writeFile("limit-ok.txt", limitContent);
    if (!expect(
        std::filesystem::file_size(tempRoot.path() / "limit-ok.txt") == limitContent.size(),
        "exact 16 MiB writeFile should create full-size file")) return 1;
    {
        std::ofstream tooLarge(tempRoot.path() / "too-large.txt", std::ios::binary | std::ios::trunc);
        tooLarge << std::string(16u * 1024u * 1024u + 1u, 'b');
    }
    if (!expectBackendException(
        "oversize readFile content should fail with StorageError",
        ErrorCode::StorageError,
        "File storage error.",
        [&store]() { (void)store.readFile("too-large.txt"); })) return 1;
    if (!expectBackendException(
        "oversize writeFile content should fail with StorageError",
        ErrorCode::StorageError,
        [&store]() { store.writeFile("oversize.txt", std::string(16u * 1024u * 1024u + 1u, 'x')); })) return 1;

    if (!expectBackendException(
        "../user_data.dat should fail with InvalidPath",
        ErrorCode::InvalidPath,
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
    std::ofstream(tempRoot.path() / "protected.TLOG" / "note.txt") << "protected";
    if (!expectBackendException(
        "protected.TLOG listDirectory should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.listDirectory("protected.TLOG"); })) return 1;
    if (!expectBackendException(
        "protected.TLOG/note.txt read should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { (void)store.readFile("protected.TLOG/note.txt"); })) return 1;
    if (!expectBackendException(
        "protected.TLOG/new.txt write should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { store.writeFile("protected.TLOG/new.txt", "x"); })) return 1;
    if (!expect(
        !std::filesystem::exists(tempRoot.path() / "missingProtected.TLOG"),
        "missingProtected.TLOG should not exist before denied write")) return 1;
    if (!expectBackendException(
        "missingProtected.TLOG/new.txt write should fail with PermissionDenied",
        ErrorCode::PermissionDenied,
        [&store]() { store.writeFile("missingProtected.TLOG/new.txt", "x"); })) return 1;
    if (!expect(
        !std::filesystem::exists(tempRoot.path() / "missingProtected.TLOG"),
        "missingProtected.TLOG should not be created by denied write")) return 1;
    if (!expectBackendException(
        "user_data.dat:stream write should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("user_data.dat:stream", "x"); })) return 1;
    if (!expectBackendException(
        "docs/new.txt:stream write should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("docs/new.txt:stream", "x"); })) return 1;
    if (!expectBackendException(
        "user_data.dat. read should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("user_data.dat."); })) return 1;
    if (!expectBackendException(
        "user_data.dat. write should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("user_data.dat.", "x"); })) return 1;
    if (!expectBackendException(
        "audit.tlog. read should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("audit.tlog."); })) return 1;
    if (!expectBackendException(
        "secret.tux trailing-space write should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("secret.tux ", "x"); })) return 1;
    if (!expectBackendException(
        "CON should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("CON"); })) return 1;
    if (!expectBackendException(
        "NUL.txt should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("NUL.txt", "x"); })) return 1;
    if (!expectBackendException(
        "COM1.log should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("COM1.log"); })) return 1;
    if (!expectBackendException(
        "USER_D~1.DAT should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { (void)store.readFile("USER_D~1.DAT"); })) return 1;
    if (!expectBackendException(
        "AUDIT~1.TLO should fail with InvalidPath",
        ErrorCode::InvalidPath,
        [&store]() { store.writeFile("AUDIT~1.TLO", "x"); })) return 1;

    const auto outside = tempRoot.path().parent_path() / (tempRoot.path().filename().string() + "_outside.txt");
    const auto outsideDir = tempRoot.path().parent_path() / (tempRoot.path().filename().string() + "_outside_dir");
    const auto rootLink = tempRoot.path().parent_path() / (tempRoot.path().filename().string() + "_root_link");
    const auto replaceRoot = tempRoot.path().parent_path() / (tempRoot.path().filename().string() + "_replace_root");
    {
        std::ofstream(outside) << "outside";
    }
    std::filesystem::create_directories(outsideDir);
    std::filesystem::remove_all(rootLink);
    std::filesystem::remove_all(replaceRoot);

    if (createdSymlink(outside, tempRoot.path() / "link-to-outside.txt")) {
        if (!expectBackendExceptionOneOf(
            "file symlink read should fail with PermissionDenied or InvalidPath",
            ErrorCode::PermissionDenied,
            ErrorCode::InvalidPath,
            [&store]() { (void)store.readFile("link-to-outside.txt"); })) return 1;
        if (!expectBackendExceptionOneOf(
            "file symlink write should fail with PermissionDenied or InvalidPath",
            ErrorCode::PermissionDenied,
            ErrorCode::InvalidPath,
            [&store]() { store.writeFile("link-to-outside.txt", "x"); })) return 1;
        const auto symlinkFilteredEntries = store.listDirectory("");
        if (!expect(
            !hasEntryNamed(symlinkFilteredEntries, "link-to-outside.txt"),
            "filesystem listDirectory should skip file symlink entries")) return 1;
    }

    if (createdDirectorySymlink(outsideDir, tempRoot.path() / "link-dir")) {
        if (!expectBackendExceptionOneOf(
            "directory symlink listDirectory should fail with PermissionDenied or InvalidPath",
            ErrorCode::PermissionDenied,
            ErrorCode::InvalidPath,
            [&store]() { (void)store.listDirectory("link-dir"); })) return 1;
        const auto symlinkFilteredEntries = store.listDirectory("");
        if (!expect(
            !hasEntryNamed(symlinkFilteredEntries, "link-dir"),
            "filesystem listDirectory should skip directory symlink entries")) return 1;
    }

    if (createdSymlink(tempRoot.path() / "missing-symlink-target.txt", tempRoot.path() / "dangling-link.txt")) {
        if (!expectBackendExceptionOneOf(
            "dangling symlink write should fail with PermissionDenied or InvalidPath",
            ErrorCode::PermissionDenied,
            ErrorCode::InvalidPath,
            [&store]() { store.writeFile("dangling-link.txt", "x"); })) return 1;
    }

    if (createdDirectorySymlink(outsideDir, rootLink)) {
        if (!expectBackendExceptionOneOf(
            "root directory symlink should fail with PermissionDenied or StorageError",
            ErrorCode::PermissionDenied,
            ErrorCode::StorageError,
            [&rootLink]() {
                FilesystemFileStore unsafeStore(rootLink.string());
                (void)unsafeStore.listDirectory("");
            })) return 1;
        std::filesystem::remove(rootLink);
    }

    std::filesystem::create_directories(replaceRoot);
    FilesystemFileStore replaceStore(replaceRoot.string());
    std::filesystem::remove_all(replaceRoot);
    if (createdDirectorySymlink(outsideDir, replaceRoot)) {
        if (!expectBackendExceptionOneOf(
            "replaced root directory symlink should fail with PermissionDenied or StorageError",
            ErrorCode::PermissionDenied,
            ErrorCode::StorageError,
            [&replaceStore]() { (void)replaceStore.listDirectory(""); })) return 1;
        std::filesystem::remove(replaceRoot);
    }

    std::filesystem::remove_all(outsideDir);
    std::filesystem::remove_all(rootLink);
    std::filesystem::remove_all(replaceRoot);
    std::filesystem::remove(outside);

    return 0;
}

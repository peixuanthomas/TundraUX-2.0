#include "backend_error.hpp"
#include "file_service.hpp"
#include "file_store.hpp"
#include "session_service.hpp"
#include "user_store.hpp"

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

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
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

    return 0;
}

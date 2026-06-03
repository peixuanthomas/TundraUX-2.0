#include "json.hpp"
#include "json_rpc.hpp"
#include "file_service.hpp"
#include "file_store.hpp"
#include "session_service.hpp"
#include "audit_service.hpp"
#include "tux_service.hpp"
#include "tux_store.hpp"
#include "user_service.hpp"
#include "user_store.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <string>
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

class EmptyInMemoryUserStore final : public tundraux::backend::UserStore {
public:
    std::vector<tundraux::backend::BackendUser> users;

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

    bool updateUser(const std::string&, const tundraux::backend::BackendUser&) override {
        return false;
    }

    bool removeUser(const std::string&) override {
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

class InMemoryFileStore final : public tundraux::backend::FileStore {
public:
    std::string writtenPath;
    std::string writtenContent;
    std::vector<std::string> calls;

    std::vector<tundraux::backend::FileEntry> listDirectory(const std::string& path) const override {
        if (path == "docs") {
            return {
                {"note.txt", "docs/note.txt", tundraux::backend::FileEntryType::File, 5}
            };
        }
        if (path == "missing") {
            throw tundraux::backend::BackendException(tundraux::backend::ErrorCode::NotFound, "File not found.");
        }
        return {};
    }

    tundraux::backend::FileContent readFile(const std::string& path) const override {
        if (path == "missing.txt") {
            throw tundraux::backend::BackendException(tundraux::backend::ErrorCode::NotFound, "File not found.");
        }
        return {"hello"};
    }

    void writeFile(const std::string& path, const std::string& content) override {
        calls.push_back("write:" + path + ":" + content);
        writtenPath = path;
        writtenContent = content;
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
        const_cast<InMemoryFileStore*>(this)->calls.push_back("search:" + root + ":" + query);
        return {
            {"match.txt", root.empty() ? "match.txt" : root + "/match.txt", tundraux::backend::FileEntryType::File, 7}
        };
    }
};

class InMemoryTuxStore final : public tundraux::backend::TuxStore {
public:
    std::map<std::string, tundraux::backend::TuxContent> files;
    std::vector<std::string> calls;

    std::vector<tundraux::backend::FileEntry> list(const std::string& path) const override {
        const_cast<InMemoryTuxStore*>(this)->calls.push_back("list:" + path);
        std::vector<tundraux::backend::FileEntry> entries;
        for (const auto& file : files) {
            entries.push_back({
                file.first,
                file.first,
                tundraux::backend::FileEntryType::File,
                file.second.content.size()
            });
        }
        return entries;
    }

    tundraux::backend::TuxMetadata metadata(const std::string& path) const override {
        const auto found = files.find(path);
        if (found == files.end()) {
            throw tundraux::backend::BackendException(tundraux::backend::ErrorCode::NotFound, "TUX file not found.");
        }
        return found->second.metadata;
    }

    tundraux::backend::TuxContent read(const std::string& path) const override {
        const_cast<InMemoryTuxStore*>(this)->calls.push_back("read:" + path);
        const auto found = files.find(path);
        if (found == files.end()) {
            throw tundraux::backend::BackendException(tundraux::backend::ErrorCode::NotFound, "TUX file not found.");
        }
        return found->second;
    }

    void create(const std::string& path, const tundraux::backend::TuxMetadata& metadata, bool overwrite) override {
        calls.push_back("create:" + path + ":" + (overwrite ? "1" : "0"));
        files[path] = tundraux::backend::TuxContent{"", metadata};
    }

    void write(const std::string& path, const std::string& content, const tundraux::backend::TuxMetadata& metadata) override {
        calls.push_back("write:" + path + ":" + content);
        files[path] = tundraux::backend::TuxContent{content, metadata};
    }

    void deleteFile(const std::string& path) override {
        calls.push_back("delete:" + path);
        files.erase(path);
    }

    void renameFile(const std::string& from, const std::string& to, bool overwrite) override {
        calls.push_back("rename:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        auto content = files.at(from);
        files.erase(from);
        files[to] = content;
    }

    void copyFile(
        const std::string& from,
        const std::string& to,
        const tundraux::backend::TuxMetadata& metadata,
        bool overwrite
    ) override {
        calls.push_back("copy:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        auto content = files.at(from);
        content.metadata = metadata;
        files[to] = content;
    }

    void moveFile(const std::string& from, const std::string& to, bool overwrite) override {
        calls.push_back("move:" + from + ":" + to + ":" + (overwrite ? "1" : "0"));
        auto content = files.at(from);
        files.erase(from);
        files[to] = content;
    }

    std::vector<tundraux::backend::FileEntry> search(const std::string& root, const std::string& query) const override {
        const_cast<InMemoryTuxStore*>(this)->calls.push_back("search:" + root + ":" + query);
        std::vector<tundraux::backend::FileEntry> entries;
        for (const auto& file : files) {
            if (file.first.find(query) != std::string::npos || file.second.content.find(query) != std::string::npos) {
                entries.push_back({
                    file.first,
                    file.first,
                    tundraux::backend::FileEntryType::File,
                    file.second.content.size()
                });
            }
        }
        return entries;
    }
};

} // namespace

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool expectInvalidJson(const std::string& input, const std::string& message) {
    const auto parsed = tundraux::backend::parseJson(input);
    return expect(!parsed.ok, message)
        && expect(!parsed.error.message.empty(), message + " error message");
}

bool expectNoErrorResponse(const std::string& response, const std::string& expectedId, const std::string& label) {
    const auto parsed = tundraux::backend::parseJson(response);
    if (!expect(parsed.ok, label + " response should parse: " + response)) return false;
    const auto& object = parsed.value.asObject();
    return expect(object.at("id").asString() == expectedId, label + " response id mismatch") &&
        expect(object.find("error") == object.end(), label + " should not return error: " + response);
}

std::filesystem::path uniqueTempPath(const std::string& label) {
    const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("tundraux_backend_json_rpc_audit_" + label + "_" + std::to_string(ticks) + "_" + std::to_string(threadId));
}

class ScopedDirectory {
public:
    explicit ScopedDirectory(std::filesystem::path path) : path_(std::move(path)) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~ScopedDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

bool runCommaLocaleNumberTest() {
    using tundraux::backend::JsonValue;
    using tundraux::backend::parseJson;
    using tundraux::backend::stringifyJson;

    const std::locale original = std::locale();
    const std::vector<std::string> localeNames = {
        "de_DE.UTF-8",
        "de-DE",
        "deu_deu.utf8",
        "German_Germany.1252",
        "fr_FR.UTF-8",
        "fr-FR"
    };

    bool foundCommaLocale = false;
    for (const std::string& localeName : localeNames) {
        try {
            const std::locale candidate(localeName.c_str());
            if (std::use_facet<std::numpunct<char>>(candidate).decimal_point() != ',') {
                continue;
            }
            std::locale::global(candidate);
            foundCommaLocale = true;
            break;
        } catch (const std::runtime_error&) {
        }
    }

    if (!foundCommaLocale) {
        return true;
    }

    const std::string json = stringifyJson(JsonValue::number(1.5));
    const auto parsed = parseJson("1.5");
    std::locale::global(original);

    return expect(json == "1.5", "number stringify should use dot decimal under comma locale: " + json)
        && expect(parsed.ok, "number parse should accept dot decimal under comma locale")
        && expect(std::fabs(parsed.value.asNumber() - 1.5) < 0.000000000001, "comma locale parsed number mismatch");
}

bool runDispatcherTest() {
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::JsonValue;
    using tundraux::backend::parseJson;
    using tundraux::backend::FileService;
    using tundraux::backend::SessionService;
    using tundraux::backend::UserService;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    InMemoryFileStore fileStore;
    FileService files(fileStore, sessions, store);
    JsonRpcDispatcher dispatcher(sessions, users, files);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto guest = parseJson(guestResponse);
    if (!expect(guest.ok, "guest response should parse: " + guestResponse)) return false;
    const auto& guestObject = guest.value.asObject();
    const auto& guestResult = guestObject.at("result").asObject();
    if (!expect(guestObject.at("id").asString() == "1", "guest response id mismatch")) return false;
    const std::string sessionId = guestResult.at("sessionId").asString();
    if (!expect(!sessionId.empty(), "guest session id should not be empty")) return false;
    if (!expect(guestResult.at("user").asObject().at("type").asString() == "guest", "guest user type mismatch")) return false;
    if (!expect(guestResult.at("user").asObject().at("name").asString().empty(), "guest user name should be empty")) return false;

    const std::string badLoginResponse = dispatcher.handleLine(
        R"({"id":"1bad","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"bad"}})"
    );
    const auto badLogin = parseJson(badLoginResponse);
    if (!expect(badLogin.ok, "bad login response should parse: " + badLoginResponse)) return false;
    const auto& badLoginError = badLogin.value.asObject().at("error").asObject();
    if (!expect(badLoginError.at("code").asString() == "AuthenticationFailed", "bad login code mismatch")) return false;
    if (!expect(badLoginError.at("message").asString() == "Incorrect password for user alice.", "bad login message mismatch")) return false;

    const std::string missingLoginResponse = dispatcher.handleLine(
        R"({"id":"1missing","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"missing","password":"bad"}})"
    );
    const auto missingLogin = parseJson(missingLoginResponse);
    if (!expect(missingLogin.ok, "missing login response should parse: " + missingLoginResponse)) return false;
    const auto& missingLoginError = missingLogin.value.asObject().at("error").asObject();
    if (!expect(missingLoginError.at("code").asString() == "AuthenticationFailed", "missing login code mismatch")) return false;
    if (!expect(missingLoginError.at("message").asString() == "User not found: missing.", "missing login message mismatch")) return false;

    const std::string forgedStartSessionResponse = dispatcher.handleLine(
        R"({"id":"1a","method":"session.startSession","params":{"user":{"name":"forged","type":"admin"}}})"
    );
    const auto forgedStartSession = parseJson(forgedStartSessionResponse);
    if (!expect(forgedStartSession.ok, "forged start session response should parse: " + forgedStartSessionResponse)) return false;
    if (!expect(forgedStartSession.value.asObject().at("id").asString() == "1a", "forged start session response id mismatch")) return false;
    if (!expect(
            forgedStartSession.value.asObject().at("error").asObject().at("code").asString() == "UnknownMethod",
            "forged start session should be rejected")) return false;

    const std::string forgedSetStrictResponse = dispatcher.handleLine(
        R"({"id":"1b","method":"user.setStrictMode","params":{"sessionId":"forged-admin-session","enabled":true}})"
    );
    const auto forgedSetStrict = parseJson(forgedSetStrictResponse);
    if (!expect(forgedSetStrict.ok, "forged set strict response should parse: " + forgedSetStrictResponse)) return false;
    if (!expect(forgedSetStrict.value.asObject().at("id").asString() == "1b", "forged set strict response id mismatch")) return false;
    if (!expect(
            forgedSetStrict.value.asObject().at("error").asObject().at("code").asString() == "SessionExpired",
            "forged set strict should fail with session expired")) return false;

    const std::string forgedCreateUserResponse = dispatcher.handleLine(
        R"({"id":"1c","method":"user.createUser","params":{"sessionId":"forged-admin-session","user":{"name":"mallory","type":"user","password":"Secret9","passwordHint":"h","failedCount":0}}})"
    );
    const auto forgedCreateUser = parseJson(forgedCreateUserResponse);
    if (!expect(forgedCreateUser.ok, "forged create user response should parse: " + forgedCreateUserResponse)) return false;
    if (!expect(forgedCreateUser.value.asObject().at("id").asString() == "1c", "forged create user response id mismatch")) return false;
    if (!expect(
            forgedCreateUser.value.asObject().at("error").asObject().at("code").asString() == "SessionExpired",
            "forged create user should fail with session expired")) return false;

    const std::string forgedDeleteUserResponse = dispatcher.handleLine(
        R"({"id":"1d","method":"user.deleteUser","params":{"sessionId":"forged-admin-session","name":"alice"}})"
    );
    const auto forgedDeleteUser = parseJson(forgedDeleteUserResponse);
    if (!expect(forgedDeleteUser.ok, "forged delete user response should parse: " + forgedDeleteUserResponse)) return false;
    if (!expect(forgedDeleteUser.value.asObject().at("id").asString() == "1d", "forged delete user response id mismatch")) return false;
    if (!expect(
            forgedDeleteUser.value.asObject().at("error").asObject().at("code").asString() == "SessionExpired",
            "forged delete user should fail with session expired")) return false;

    const std::string loginResponse = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Secret1"}})"
    );
    const auto login = parseJson(loginResponse);
    if (!expect(login.ok, "login response should parse: " + loginResponse)) return false;
    const auto& loginResult = login.value.asObject().at("result").asObject();
    if (!expect(loginResult.at("user").asObject().at("type").asString() == "admin", "login user type mismatch")) return false;

    const std::string listResponse = dispatcher.handleLine(
        R"({"id":"3","method":"user.listUsers","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto list = parseJson(listResponse);
    if (!expect(list.ok, "list response should parse: " + listResponse)) return false;
    const auto& listUsers = list.value.asObject().at("result").asObject().at("users").asArray();
    if (!expect(listUsers.size() == 2, "list users count mismatch")) return false;
    for (const auto& user : listUsers) {
        if (!expect(user.asObject().find("password") == user.asObject().end(), "list users should not expose password field")) return false;
    }

    const std::string listDirectoryResponse = dispatcher.handleLine(
        R"({"id":"5","method":"file.listDirectory","params":{"sessionId":")" + sessionId + R"(","path":"docs"}})"
    );
    const auto listDirectory = parseJson(listDirectoryResponse);
    if (!expect(listDirectory.ok, "list directory response should parse: " + listDirectoryResponse)) return false;
    const auto& entries = listDirectory.value.asObject().at("result").asObject().at("entries").asArray();
    if (!expect(entries.size() == 1, "list directory entry count mismatch")) return false;
    const auto& entry = entries[0].asObject();
    if (!expect(entry.at("name").asString() == "note.txt", "list directory entry name mismatch")) return false;
    if (!expect(entry.at("path").asString() == "docs/note.txt", "list directory entry path mismatch")) return false;
    if (!expect(entry.at("type").asString() == "file", "list directory entry type mismatch")) return false;
    if (!expect(entry.at("size").asNumber() == 5.0, "list directory entry size mismatch")) return false;

    const std::string readFileResponse = dispatcher.handleLine(
        R"({"id":"6","method":"file.readFile","params":{"sessionId":")" + sessionId + R"(","path":"docs/note.txt"}})"
    );
    const auto readFile = parseJson(readFileResponse);
    if (!expect(readFile.ok, "read file response should parse: " + readFileResponse)) return false;
    if (!expect(readFile.value.asObject().at("result").asObject().at("content").asString() == "hello", "read file content mismatch")) return false;

    const std::string writeFileResponse = dispatcher.handleLine(
        R"({"id":"7","method":"file.writeFile","params":{"sessionId":")" + sessionId + R"(","path":"docs/note.txt","content":"updated"}})"
    );
    const auto writeFile = parseJson(writeFileResponse);
    if (!expect(writeFile.ok, "write file response should parse: " + writeFileResponse)) return false;
    if (!expect(writeFile.value.asObject().at("result").asObject().at("ok").asBoolean(), "write file ok mismatch")) return false;
    if (!expect(fileStore.writtenPath == "docs/note.txt", "write file path mismatch")) return false;
    if (!expect(fileStore.writtenContent == "updated", "write file content mismatch")) return false;

    const std::string missingFileResponse = dispatcher.handleLine(
        R"({"id":"8","method":"file.readFile","params":{"sessionId":")" + sessionId + R"(","path":"missing.txt"}})"
    );
    const auto missingFile = parseJson(missingFileResponse);
    if (!expect(missingFile.ok, "missing file response should parse: " + missingFileResponse)) return false;
    if (!expect(missingFile.value.asObject().at("error").asObject().at("code").asString() == "NotFound", "missing file code mismatch")) return false;

    const std::string invalidFileParamsResponse = dispatcher.handleLine(
        R"({"id":"9","method":"file.writeFile","params":{"sessionId":")" + sessionId + R"(","path":"docs/note.txt"}})"
    );
    const auto invalidFileParams = parseJson(invalidFileParamsResponse);
    if (!expect(invalidFileParams.ok, "invalid file params response should parse: " + invalidFileParamsResponse)) return false;
    if (!expect(invalidFileParams.value.asObject().at("error").asObject().at("code").asString() == "InvalidParams", "invalid file params code mismatch")) return false;

    const std::string missingStrictModeEnabledResponse = dispatcher.handleLine(
        R"({"id":"15","method":"user.setStrictMode","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto missingStrictModeEnabled = parseJson(missingStrictModeEnabledResponse);
    if (!expect(missingStrictModeEnabled.ok, "missing strict mode enabled response should parse: " + missingStrictModeEnabledResponse)) return false;
    if (!expect(missingStrictModeEnabled.value.asObject().at("id").asString() == "15", "missing strict mode enabled response id mismatch")) return false;
    if (!expect(missingStrictModeEnabled.value.asObject().at("error").asObject().at("code").asString() == "InvalidParams", "missing strict mode enabled code mismatch")) return false;

    const std::string logoutResponse = dispatcher.handleLine(
        R"({"id":"10","method":"session.logout","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto logout = parseJson(logoutResponse);
    if (!expect(logout.ok, "logout response should parse: " + logoutResponse)) return false;
    const auto& logoutObject = logout.value.asObject();
    if (!expect(logoutObject.at("id").asString() == "10", "logout response id mismatch")) return false;
    if (!expect(logoutObject.find("error") == logoutObject.end(), "logout should not return error")) return false;
    if (!expect(logoutObject.at("result").type() == JsonValue::Type::Object, "logout result should be an object")) return false;

    const std::string whoamiAfterLogoutResponse = dispatcher.handleLine(
        R"({"id":"11","method":"session.whoami","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto whoamiAfterLogout = parseJson(whoamiAfterLogoutResponse);
    if (!expect(whoamiAfterLogout.ok, "whoami after logout response should parse: " + whoamiAfterLogoutResponse)) return false;
    const auto& whoamiAfterLogoutUser = whoamiAfterLogout.value.asObject().at("result").asObject().at("user").asObject();
    if (!expect(whoamiAfterLogout.value.asObject().at("id").asString() == "11", "whoami after logout response id mismatch")) return false;
    if (!expect(whoamiAfterLogoutUser.at("type").asString() == "guest", "whoami after logout user type mismatch")) return false;
    if (!expect(whoamiAfterLogoutUser.at("name").asString().empty(), "whoami after logout user name should be empty")) return false;

    const std::string guestListResponse = dispatcher.handleLine(
        R"({"id":"12","method":"user.listUsers","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto guestList = parseJson(guestListResponse);
    if (!expect(guestList.ok, "guest list response should parse: " + guestListResponse)) return false;
    if (!expect(guestList.value.asObject().at("id").asString() == "12", "guest list response id mismatch")) return false;
    if (!expect(guestList.value.asObject().at("error").asObject().at("code").asString() == "PermissionDenied", "guest list users code mismatch")) return false;

    const std::string guestFileListResponse = dispatcher.handleLine(
        R"({"id":"14","method":"file.listDirectory","params":{"sessionId":")" + sessionId + R"(","path":"docs"}})"
    );
    const auto guestFileList = parseJson(guestFileListResponse);
    if (!expect(guestFileList.ok, "guest file list response should parse: " + guestFileListResponse)) return false;
    if (!expect(guestFileList.value.asObject().at("error").asObject().at("code").asString() == "PermissionDenied", "guest file list code mismatch")) return false;

    const std::string invalidLoginParamsResponse = dispatcher.handleLine(
        R"({"id":"13","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice"}})"
    );
    const auto invalidLoginParams = parseJson(invalidLoginParamsResponse);
    if (!expect(invalidLoginParams.ok, "invalid login params response should parse: " + invalidLoginParamsResponse)) return false;
    if (!expect(invalidLoginParams.value.asObject().at("id").asString() == "13", "invalid login params response id mismatch")) return false;
    if (!expect(invalidLoginParams.value.asObject().at("error").asObject().at("code").asString() == "InvalidParams", "invalid login params code mismatch")) return false;

    const std::string unknownResponse = dispatcher.handleLine(R"({"id":"4","method":"missing.method","params":{}})");
    const auto unknown = parseJson(unknownResponse);
    if (!expect(unknown.ok, "unknown response should parse: " + unknownResponse)) return false;
    if (!expect(unknown.value.asObject().at("error").asObject().at("code").asString() == "UnknownMethod", "unknown method code mismatch")) return false;

    const std::string invalidResponse = dispatcher.handleLine("{");
    const auto invalid = parseJson(invalidResponse);
    if (!expect(invalid.ok, "invalid response should parse: " + invalidResponse)) return false;
    const auto& invalidObject = invalid.value.asObject();
    if (!expect(invalidObject.at("id").type() == JsonValue::Type::Null, "invalid request id should be null")) return false;
    if (!expect(invalidObject.at("error").asObject().at("code").asString() == "InvalidRequest", "invalid request code mismatch")) return false;

    return true;
}

bool runDispatcherUserMutationTest() {
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::parseJson;
    using tundraux::backend::SessionService;
    using tundraux::backend::UserService;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    JsonRpcDispatcher dispatcher(sessions, users);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto guest = parseJson(guestResponse);
    if (!expect(guest.ok, "user mutation guest response should parse: " + guestResponse)) return false;
    const std::string sessionId = guest.value.asObject().at("result").asObject().at("sessionId").asString();

    const std::string loginResponse = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Secret1"}})"
    );
    if (!expectNoErrorResponse(loginResponse, "2", "user mutation login")) return false;

    const std::string createResponse = dispatcher.handleLine(
        R"({"id":"3","method":"user.createUser","params":{"sessionId":")" + sessionId +
        R"(","user":{"name":"carol","type":"user","password":"Secret3","passwordHint":"team lead","failedCount":2}}})"
    );
    if (!expectNoErrorResponse(createResponse, "3", "user mutation create")) return false;
    auto carol = std::find_if(store.users.begin(), store.users.end(), [](const auto& user) {
        return user.name == "carol";
    });
    if (!expect(carol != store.users.end(), "created user should be stored")) return false;
    if (!expect(carol->type == "user", "created user type mismatch")) return false;
    if (!expect(carol->password == "Secret3", "created user password mismatch")) return false;
    if (!expect(carol->passwordHint == "team lead", "created user hint mismatch")) return false;
    if (!expect(carol->failedCount == 2, "created user failed count mismatch")) return false;

    const std::string updateResponse = dispatcher.handleLine(
        R"({"id":"4","method":"user.updateUser","params":{"sessionId":")" + sessionId +
        R"(","originalName":"carol","passwordProvided":false,"user":{"name":"carol","type":"admin","passwordHint":"ops","failedCount":4}}})"
    );
    if (!expectNoErrorResponse(updateResponse, "4", "user mutation update")) return false;
    carol = std::find_if(store.users.begin(), store.users.end(), [](const auto& user) {
        return user.name == "carol";
    });
    if (!expect(carol != store.users.end(), "updated user should remain stored")) return false;
    if (!expect(carol->type == "admin", "updated user type mismatch")) return false;
    if (!expect(carol->password == "Secret3", "update without password should preserve password")) return false;
    if (!expect(carol->passwordHint == "ops", "updated user hint mismatch")) return false;
    if (!expect(carol->failedCount == 4, "updated user failed count mismatch")) return false;

    const std::string resetResponse = dispatcher.handleLine(
        R"({"id":"5","method":"user.resetFailedCount","params":{"sessionId":")" + sessionId +
        R"(","name":"carol"}})"
    );
    if (!expectNoErrorResponse(resetResponse, "5", "user mutation reset")) return false;
    carol = std::find_if(store.users.begin(), store.users.end(), [](const auto& user) {
        return user.name == "carol";
    });
    if (!expect(carol != store.users.end() && carol->failedCount == 0, "reset should clear failed count")) return false;

    const std::string disableResponse = dispatcher.handleLine(
        R"({"id":"6","method":"user.disableUser","params":{"sessionId":")" + sessionId +
        R"(","name":"carol"}})"
    );
    if (!expectNoErrorResponse(disableResponse, "6", "user mutation disable")) return false;
    carol = std::find_if(store.users.begin(), store.users.end(), [](const auto& user) {
        return user.name == "carol";
    });
    if (!expect(carol != store.users.end() && carol->failedCount == 8, "disable should lock user")) return false;

    const std::string deleteResponse = dispatcher.handleLine(
        R"({"id":"7","method":"user.deleteUser","params":{"sessionId":")" + sessionId +
        R"(","name":"carol"}})"
    );
    if (!expectNoErrorResponse(deleteResponse, "7", "user mutation delete")) return false;
    carol = std::find_if(store.users.begin(), store.users.end(), [](const auto& user) {
        return user.name == "carol";
    });
    return expect(carol == store.users.end(), "deleted user should be removed");
}

bool runDispatcherFileMutationTest() {
    using tundraux::backend::FileService;
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::parseJson;
    using tundraux::backend::SessionService;
    using tundraux::backend::TuxService;
    using tundraux::backend::UserService;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    InMemoryFileStore fileStore;
    FileService files(fileStore, sessions, store);
    InMemoryTuxStore tuxStore;
    TuxService tux(tuxStore, sessions, store);
    JsonRpcDispatcher dispatcher(sessions, users, files, tux);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto guest = parseJson(guestResponse);
    if (!expect(guest.ok, "file mutation guest response should parse: " + guestResponse)) return false;
    const std::string sessionId = guest.value.asObject().at("result").asObject().at("sessionId").asString();

    const std::string loginResponse = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Secret1"}})"
    );
    if (!expectNoErrorResponse(loginResponse, "2", "file mutation login")) return false;

    const std::vector<std::pair<std::string, std::string>> requests{
        {"3", R"({"id":"3","method":"file.createDirectory","params":{"sessionId":")" + sessionId + R"(","path":"docs"}})"},
        {"4", R"({"id":"4","method":"file.writeFile","params":{"sessionId":")" + sessionId + R"(","path":"docs/note.txt","content":"updated"}})"},
        {"5", R"({"id":"5","method":"file.deleteFile","params":{"sessionId":")" + sessionId + R"(","path":"docs/old.txt"}})"},
        {"6", R"({"id":"6","method":"file.renameFile","params":{"sessionId":")" + sessionId + R"(","from":"docs/a.txt","to":"docs/b.txt","overwrite":false}})"},
        {"7", R"({"id":"7","method":"file.copyFile","params":{"sessionId":")" + sessionId + R"(","from":"docs/b.txt","to":"docs/c.txt","overwrite":true}})"},
        {"8", R"({"id":"8","method":"file.moveFile","params":{"sessionId":")" + sessionId + R"(","from":"docs/c.txt","to":"archive/c.txt","overwrite":false}})"},
        {"9", R"({"id":"9","method":"file.removeDirectory","params":{"sessionId":")" + sessionId + R"(","path":"archive","recursive":true}})"}
    };
    for (const auto& request : requests) {
        if (!expectNoErrorResponse(dispatcher.handleLine(request.second), request.first, "file mutation " + request.first)) {
            return false;
        }
    }

    const std::string searchResponse = dispatcher.handleLine(
        R"({"id":"10","method":"file.search","params":{"sessionId":")" + sessionId +
        R"(","root":"docs","query":"match"}})"
    );
    if (!expectNoErrorResponse(searchResponse, "10", "file search")) return false;
    const auto search = parseJson(searchResponse);
    const auto& entries = search.value.asObject().at("result").asObject().at("entries").asArray();
    if (!expect(entries.size() == 1, "file search entry count mismatch")) return false;
    const auto& entry = entries[0].asObject();
    if (!expect(entry.at("name").asString() == "match.txt", "file search entry name mismatch")) return false;
    if (!expect(entry.at("path").asString() == "docs/match.txt", "file search entry path mismatch")) return false;

    const std::vector<std::string> expectedCalls{
        "mkdir:docs",
        "write:docs/note.txt:updated",
        "delete:docs/old.txt",
        "rename:docs/a.txt:docs/b.txt:0",
        "copy:docs/b.txt:docs/c.txt:1",
        "move:docs/c.txt:archive/c.txt:0",
        "rmdir:archive:1",
        "search:docs:match"
    };
    return expect(fileStore.calls == expectedCalls, "file mutation call sequence mismatch");
}

bool runDispatcherTuxMethodsTest() {
    using tundraux::backend::FileService;
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::parseJson;
    using tundraux::backend::SessionService;
    using tundraux::backend::TuxService;
    using tundraux::backend::UserService;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    InMemoryFileStore fileStore;
    FileService files(fileStore, sessions, store);
    InMemoryTuxStore tuxStore;
    TuxService tux(tuxStore, sessions, store);
    JsonRpcDispatcher dispatcher(sessions, users, files, tux);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto guest = parseJson(guestResponse);
    if (!expect(guest.ok, "tux guest response should parse: " + guestResponse)) return false;
    const std::string sessionId = guest.value.asObject().at("result").asObject().at("sessionId").asString();

    const std::string loginResponse = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Secret1"}})"
    );
    if (!expectNoErrorResponse(loginResponse, "2", "tux login")) return false;

    const std::vector<std::pair<std::string, std::string>> mutationRequests{
        {"3", R"({"id":"3","method":"tux.create","params":{"sessionId":")" + sessionId + R"(","path":"docs/secret","overwrite":false}})"},
        {"4", R"({"id":"4","method":"tux.write","params":{"sessionId":")" + sessionId + R"(","path":"docs/secret","content":"hello"}})"}
    };
    for (const auto& request : mutationRequests) {
        if (!expectNoErrorResponse(dispatcher.handleLine(request.second), request.first, "tux mutation " + request.first)) {
            return false;
        }
    }

    const std::string readResponse = dispatcher.handleLine(
        R"({"id":"5","method":"tux.read","params":{"sessionId":")" + sessionId + R"(","path":"docs/secret"}})"
    );
    if (!expectNoErrorResponse(readResponse, "5", "tux read")) return false;
    const auto read = parseJson(readResponse);
    const auto& readResult = read.value.asObject().at("result").asObject();
    if (!expect(readResult.at("content").asString() == "hello", "tux read content mismatch")) return false;
    if (!expect(readResult.at("creator").asString() == "alice", "tux read creator mismatch")) return false;
    if (!expect(readResult.at("lastEditor").asString() == "alice", "tux read last editor mismatch")) return false;

    const std::string listResponse = dispatcher.handleLine(
        R"({"id":"6","method":"tux.list","params":{"sessionId":")" + sessionId + R"(","path":"docs"}})"
    );
    if (!expectNoErrorResponse(listResponse, "6", "tux list")) return false;
    const auto list = parseJson(listResponse);
    if (!expect(!list.value.asObject().at("result").asObject().at("entries").asArray().empty(), "tux list entries should not be empty")) {
        return false;
    }

    const std::string searchResponse = dispatcher.handleLine(
        R"({"id":"7","method":"tux.search","params":{"sessionId":")" + sessionId + R"(","root":"docs","query":"hello"}})"
    );
    if (!expectNoErrorResponse(searchResponse, "7", "tux search")) return false;
    const auto search = parseJson(searchResponse);
    const auto& searchEntries = search.value.asObject().at("result").asObject().at("entries").asArray();
    if (!expect(searchEntries.size() == 1, "tux search entry count mismatch")) return false;
    if (!expect(searchEntries[0].asObject().at("path").asString() == "docs/secret", "tux search entry path mismatch")) return false;

    const std::vector<std::pair<std::string, std::string>> remainingRequests{
        {"8", R"({"id":"8","method":"tux.rename","params":{"sessionId":")" + sessionId + R"(","from":"docs/secret","to":"docs/renamed","overwrite":false}})"},
        {"9", R"({"id":"9","method":"tux.copy","params":{"sessionId":")" + sessionId + R"(","from":"docs/renamed","to":"docs/copy","overwrite":true}})"},
        {"10", R"({"id":"10","method":"tux.move","params":{"sessionId":")" + sessionId + R"(","from":"docs/copy","to":"docs/moved","overwrite":false}})"},
        {"11", R"({"id":"11","method":"tux.delete","params":{"sessionId":")" + sessionId + R"(","path":"docs/moved"}})"}
    };
    for (const auto& request : remainingRequests) {
        if (!expectNoErrorResponse(dispatcher.handleLine(request.second), request.first, "tux remaining " + request.first)) {
            return false;
        }
    }

    const std::vector<std::string> expectedCalls{
        "create:docs/secret:0",
        "write:docs/secret:hello",
        "read:docs/secret",
        "list:docs",
        "search:docs:hello",
        "rename:docs/secret:docs/renamed:0",
        "copy:docs/renamed:docs/copy:1",
        "move:docs/copy:docs/moved:0",
        "delete:docs/moved"
    };
    return expect(tuxStore.calls == expectedCalls, "tux call sequence mismatch");
}

bool runDispatcherWithoutFileServiceTest() {
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::parseJson;
    using tundraux::backend::SessionService;
    using tundraux::backend::UserService;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    JsonRpcDispatcher dispatcher(sessions, users);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto guest = parseJson(guestResponse);
    if (!expect(guest.ok, "two-arg guest response should parse: " + guestResponse)) return false;
    if (!expect(guest.value.asObject().at("id").asString() == "1", "two-arg guest response id mismatch")) return false;
    if (!expect(guest.value.asObject().find("error") == guest.value.asObject().end(), "two-arg guest should not return error")) return false;
    const std::string sessionId = guest.value.asObject().at("result").asObject().at("sessionId").asString();

    const std::string fileResponse = dispatcher.handleLine(
        R"({"id":"2","method":"file.readFile","params":{"sessionId":")" + sessionId + R"(","path":"docs/note.txt"}})"
    );
    const auto file = parseJson(fileResponse);
    if (!expect(file.ok, "two-arg file response should parse: " + fileResponse)) return false;
    if (!expect(file.value.asObject().at("id").asString() == "2", "two-arg file response id mismatch")) return false;
    if (!expect(file.value.asObject().at("error").asObject().at("code").asString() == "UnknownMethod", "two-arg file code mismatch")) return false;

    const std::string auditResponse = dispatcher.handleLine(
        R"({"id":"3","method":"audit.readTlog","params":{"sessionId":")" + sessionId + R"(","path":"audit.tlog"}})"
    );
    const auto audit = parseJson(auditResponse);
    if (!expect(audit.ok, "two-arg audit response should parse: " + auditResponse)) return false;
    if (!expect(audit.value.asObject().at("id").asString() == "3", "two-arg audit response id mismatch")) return false;
    if (!expect(
            audit.value.asObject().at("error").asObject().at("code").asString() == "UnknownMethod",
            "two-arg audit method should be unknown"
    )) return false;

    return true;
}

bool runDispatcherSetupCreateInitialAdminTest() {
    using tundraux::backend::SessionService;
    using tundraux::backend::UserService;
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::parseJson;

    EmptyInMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    JsonRpcDispatcher dispatcher(sessions, users);

    const std::string firstResponse = dispatcher.handleLine(R"({
        "id":"1",
        "method":"setup.createInitialAdmin",
        "params":{"sessionId":"setup-session","username":"admin","password":"Secret1","passwordHint":"primary"}
    })");

    const auto first = parseJson(firstResponse);
    if (!expect(first.ok, "setup.createInitialAdmin response should parse: " + firstResponse)) return false;
    const auto& firstObject = first.value.asObject();
    if (!expect(firstObject.at("id").asString() == "1", "setup.createInitialAdmin response id mismatch")) return false;
    if (!expect(firstObject.find("error") == firstObject.end(), "setup.createInitialAdmin should return no error")) return false;
    const auto& firstResult = firstObject.at("result").asObject();
    if (!expect(firstResult.at("ok").asBoolean(), "setup.createInitialAdmin should return ok true")) return false;

    const std::string secondResponse = dispatcher.handleLine(R"({
        "id":"2",
        "method":"setup.createInitialAdmin",
        "params":{"sessionId":"setup-session","username":"second","password":"Secret2","passwordHint":"secondary"}
    })");

    const auto second = parseJson(secondResponse);
    if (!expect(second.ok, "repeated setup.createInitialAdmin response should parse: " + secondResponse)) return false;
    const auto& secondObject = second.value.asObject();
    if (!expect(secondObject.at("id").asString() == "2", "repeated setup.createInitialAdmin response id mismatch")) return false;
    const auto& secondError = secondObject.at("error").asObject();
    if (!expect(secondError.at("code").asString() == "PermissionDenied", "setup already initialized should be PermissionDenied")) return false;
    if (!expect(secondError.at("message").asString() == "Setup already initialized.", "setup already initialized message mismatch")) return false;

    return true;
}

bool runDispatcherAuditMethodsTest() {
    using tundraux::backend::AuditService;
    using tundraux::backend::FileService;
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::SessionService;
    using tundraux::backend::TuxService;
    using tundraux::backend::UserService;
    using tundraux::backend::parseJson;

    InMemoryUserStore store;
    store.strictMode = true;
    SessionService sessions(store);
    UserService users(store, sessions);
    InMemoryFileStore fileStore;
    FileService files(fileStore, sessions, store);
    InMemoryTuxStore tuxStore;
    TuxService tux(tuxStore, sessions, store);
    ScopedDirectory logs(uniqueTempPath("dispatcher"));
    AuditService audit(store, sessions, logs.path().string());
    JsonRpcDispatcher dispatcher(sessions, users, files, tux, "", &audit);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto guest = parseJson(guestResponse);
    if (!expect(guest.ok, "audit method guest response should parse: " + guestResponse)) return false;
    const auto guestObject = guest.value.asObject();
    const auto guestResultEntry = guestObject.find("result");
    if (!expect(guestResultEntry != guestObject.end(), "audit method guest should include result")) return false;
    const auto guestSessionEntry = guestResultEntry->second.asObject().find("sessionId");
    if (!expect(guestSessionEntry != guestResultEntry->second.asObject().end(), "audit method guest should include sessionId")) return false;
    const std::string guestSessionId = guestSessionEntry->second.asString();

    const std::string guestLogResponse = dispatcher.handleLine(
        R"({"id":"1guest-log","method":"audit.logEvent","params":{"sessionId":")" + guestSessionId +
        R"(","category":"login","detail":"backend attempt"}})"
    );
    if (!expectNoErrorResponse(guestLogResponse, "1guest-log", "guest audit.logEvent")) return false;

    const std::string guestKeyResponse = dispatcher.handleLine(
        R"({"id":"1guest-key","method":"audit.logKeyPress","params":{"sessionId":")" + guestSessionId +
        R"(","key":"p","sensitive":true}})"
    );
    if (!expectNoErrorResponse(guestKeyResponse, "1guest-key", "guest audit.logKeyPress")) return false;

    const std::string guestExportResponse = dispatcher.handleLine(
        R"({"id":"1guest-export","method":"audit.exportTlog","params":{"sessionId":")" + guestSessionId +
        R"(","path":"audit.tlog"}})"
    );
    const auto guestExport = parseJson(guestExportResponse);
    if (!expect(guestExport.ok, "guest audit.exportTlog should parse: " + guestExportResponse)) return false;
    if (!expect(guestExport.value.asObject().find("error") != guestExport.value.asObject().end(),
            "guest should receive audit.exportTlog error")) return false;
    if (!expect(
            guestExport.value.asObject().at("error").asObject().at("code").asString() == "PermissionDenied",
            "guest should be denied audit.exportTlog")) return false;

    const std::string loginResponse = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + guestSessionId +
        R"(","username":"alice","password":"Secret1"}})"
    );
    if (!expectNoErrorResponse(loginResponse, "2", "audit method login")) return false;
    const auto parsedLogin = parseJson(loginResponse);
    if (!expect(parsedLogin.ok, "audit method login response should re-parse")) return false;
    const auto loginObject = parsedLogin.value.asObject();
    const auto loginResult = loginObject.find("result") != loginObject.end() ? &loginObject.at("result").asObject() : nullptr;
    if (!expect(loginResult != nullptr, "audit method login should include result")) return false;
    const auto loginSessionEntry = loginResult->find("sessionId");
    if (!expect(loginSessionEntry != loginResult->end(), "audit method login should include sessionId")) return false;
    const std::string adminSessionId = loginSessionEntry->second.asString();

    const std::string logResponse = dispatcher.handleLine(
        R"({"id":"3","method":"audit.logEvent","params":{"sessionId":")" + adminSessionId +
        R"(","category":"shell","detail":"shell command"}})"
    );
    if (!expectNoErrorResponse(logResponse, "3", "audit.logEvent")) return false;

    const std::string keyResponse = dispatcher.handleLine(
        R"({"id":"4","method":"audit.logKeyPress","params":{"sessionId":")" + adminSessionId +
        R"(","key":"x","sensitive":true}})"
    );
    if (!expectNoErrorResponse(keyResponse, "4", "audit.logKeyPress")) return false;

    const std::string readResponse = dispatcher.handleLine(
        R"({"id":"5","method":"audit.readTlog","params":{"sessionId":")" + adminSessionId +
        R"(","path":"audit.tlog"}})"
    );
    const auto read = parseJson(readResponse);
    if (!expect(read.ok, "audit.readTlog response should parse: " + readResponse)) return false;
    if (!expect(read.value.asObject().at("id").asString() == "5", "audit.readTlog response id mismatch")) return false;
    const auto readObject = read.value.asObject();
    if (!expect(readObject.find("result") != readObject.end(), "audit.readTlog should include result")) return false;
    const auto& readResult = readObject.at("result").asObject();
    const auto readLinesEntry = readResult.find("lines");
    if (!expect(readLinesEntry != readResult.end(), "audit.readTlog should include lines")) return false;
    const auto readLines = readLinesEntry->second.asArray();
    if (!expect(readLines.size() == 4, "audit.readTlog should return guest and admin audit lines")) return false;
    bool hasSanitized = false;
    bool hasGuestLogin = false;
    for (const auto& entry : readLines) {
        if (entry.asString().find("Character [redacted]") != std::string::npos) {
            hasSanitized = true;
        }
        if (entry.asString().find("type=guest") != std::string::npos &&
            entry.asString().find("backend attempt") != std::string::npos) {
            hasGuestLogin = true;
        }
    }
    if (!expect(hasSanitized, "audit.readTlog should redact sensitive key presses")) return false;
    if (!expect(hasGuestLogin, "audit.readTlog should include pre-login guest audit event")) return false;

    const std::string exportResponse = dispatcher.handleLine(
        R"({"id":"6","method":"audit.exportTlog","params":{"sessionId":")" + adminSessionId +
        R"(","path":"audit.tlog"}})"
    );
    const auto exported = parseJson(exportResponse);
    if (!expect(exported.ok, "audit.exportTlog response should parse: " + exportResponse)) return false;
    if (!expect(exported.value.asObject().at("id").asString() == "6", "audit.exportTlog response id mismatch")) return false;
    const auto exportObject = exported.value.asObject();
    const auto exportResultEntry = exportObject.find("result");
    if (!expect(exportResultEntry != exportObject.end(), "audit.exportTlog should include result")) return false;
    const auto exportContentEntry = exportResultEntry->second.asObject().find("content");
    if (!expect(exportContentEntry != exportResultEntry->second.asObject().end(), "audit.exportTlog should include content")) return false;
    if (!expect(
            exportContentEntry->second.asString().find("Character [redacted]") != std::string::npos,
            "audit.exportTlog should include redacted key press detail"
    )) return false;

    const std::string deniedGuestResponse = dispatcher.handleLine(R"({"id":"7","method":"session.startGuestSession","params":{}})");
    const auto deniedGuest = parseJson(deniedGuestResponse);
    if (!expect(deniedGuest.ok, "guest read denial should start a guest session")) return false;
    const auto deniedGuestObject = deniedGuest.value.asObject();
    const auto deniedGuestResultEntry = deniedGuestObject.find("result");
    if (!expect(deniedGuestResultEntry != deniedGuestObject.end(), "guest read denial should include result")) return false;
    const auto deniedGuestSessionEntry = deniedGuestResultEntry->second.asObject().find("sessionId");
    if (!expect(deniedGuestSessionEntry != deniedGuestResultEntry->second.asObject().end(), "guest read denial should include sessionId")) return false;
    const std::string deniedGuestSessionId = deniedGuestSessionEntry->second.asString();

    const std::string deniedResponse = dispatcher.handleLine(
        R"({"id":"8","method":"audit.readTlog","params":{"sessionId":")" + deniedGuestSessionId +
        R"(","path":"audit.tlog"}})"
    );
    const auto denied = parseJson(deniedResponse);
    if (!expect(denied.ok, "guest audit.readTlog should parse: " + deniedResponse)) return false;
    const auto deniedObject = denied.value.asObject();
    if (!expect(deniedObject.find("error") != deniedObject.end(), "guest should receive audit.readTlog error")) return false;
    const auto deniedError = deniedObject.at("error").asObject();
    if (!expect(
            deniedError.find("code") != deniedError.end() &&
            deniedError.at("code").asString() == "PermissionDenied",
            "guest should be denied audit.readTlog")) return false;

    return true;
}

int main() {
    using tundraux::backend::JsonValue;
    using tundraux::backend::parseJson;
    using tundraux::backend::stringifyJson;

    const auto parsed = parseJson(R"({"id":"1","method":"session.whoami","params":{"sessionId":"session-1"}})");
    if (!expect(parsed.ok, "json parse should pass")) return 1;
    if (!expect(parsed.value.asObject().at("id").asString() == "1", "id mismatch")) return 1;
    if (!expect(parsed.value.asObject().at("params").asObject().at("sessionId").asString() == "session-1", "session id mismatch")) return 1;

    JsonValue object = JsonValue::object({
        {"id", JsonValue::string("1")},
        {"result", JsonValue::object({{"ok", JsonValue::boolean(true)}})}
    });
    const std::string json = stringifyJson(object);
    if (!expect(json == R"({"id":"1","result":{"ok":true}})", "json stringify mismatch: " + json)) return 1;

    const auto invalid = parseJson("{");
    if (!expect(!invalid.ok, "invalid json should fail")) return 1;

    if (!expectInvalidJson("01", "leading zero number should fail")) return 1;
    if (!expectInvalidJson("00", "double zero number should fail")) return 1;
    if (!expectInvalidJson("-01", "negative leading zero number should fail")) return 1;
    if (!expectInvalidJson("00.5", "leading zero decimal should fail")) return 1;
    if (!expectInvalidJson("1.", "missing decimal digit should fail")) return 1;
    if (!expectInvalidJson("-.1", "missing integer digit should fail")) return 1;
    if (!expectInvalidJson("[1,]", "trailing array comma should fail")) return 1;
    if (!expectInvalidJson(R"({"a":1,})", "trailing object comma should fail")) return 1;
    if (!expectInvalidJson(R"({"id":"a","id":"b"})", "duplicate object key should fail")) return 1;
    if (!expectInvalidJson("1e3", "exponent notation should remain unsupported")) return 1;
    if (!expectInvalidJson("\f{}", "form feed outside string should fail")) return 1;
    if (!expectInvalidJson("[1\v]", "vertical tab outside string should fail")) return 1;
    if (!expectInvalidJson(std::string("\"line\nbreak\""), "raw newline in string should fail")) return 1;

    const auto escaped = parseJson(R"("line\n\tbreak")");
    if (!expect(escaped.ok, "escaped newline and tab should parse")) return 1;
    if (!expect(escaped.value.asString() == std::string("line\n\tbreak"), "escaped string mismatch")) return 1;
    const auto simpleEscapes = parseJson(R"("\b\f\/")");
    if (!expect(simpleEscapes.ok, "backspace form-feed slash escapes should parse")) return 1;
    if (!expect(simpleEscapes.value.asString() == std::string("\b\f/"), "simple escape string mismatch")) return 1;

    const std::string controlJson = stringifyJson(JsonValue::string(std::string("a\001b", 3)));
    if (!expect(controlJson == R"("a\u0001b")", "control character should stringify as unicode escape: " + controlJson)) return 1;
    const auto reparsedControl = parseJson(controlJson);
    if (!expect(reparsedControl.ok, "unicode escaped control character should parse")) return 1;
    if (!expect(reparsedControl.value.asString() == std::string("a\001b", 3), "unicode escaped control character mismatch")) return 1;

    const std::string smallNumberJson = stringifyJson(JsonValue::number(0.000001));
    if (!expect(smallNumberJson == "0.000001", "small number should not use exponent notation: " + smallNumberJson)) return 1;
    const auto reparsedSmallNumber = parseJson(smallNumberJson);
    if (!expect(reparsedSmallNumber.ok, "small number should parse after stringify")) return 1;
    if (!expect(std::fabs(reparsedSmallNumber.value.asNumber() - 0.000001) < 0.000000000001, "small number round-trip mismatch")) return 1;
    if (!expect(stringifyJson(JsonValue::number(std::numeric_limits<double>::infinity())) == "null", "infinity should stringify safely")) return 1;
    if (!expect(stringifyJson(JsonValue::number(std::numeric_limits<double>::quiet_NaN())) == "null", "nan should stringify safely")) return 1;
    if (!expect(runCommaLocaleNumberTest(), "comma locale number behavior failed")) return 1;

    bool wrongTypeThrew = false;
    try {
        JsonValue::boolean(true).asString();
    } catch (const std::logic_error&) {
        wrongTypeThrew = true;
    }
    if (!expect(wrongTypeThrew, "wrong-type accessor should throw")) return 1;

    if (!expect(runDispatcherTest(), "json rpc dispatcher behavior failed")) return 1;
    if (!expect(runDispatcherUserMutationTest(), "json rpc user mutation behavior failed")) return 1;
    if (!expect(runDispatcherFileMutationTest(), "json rpc file mutation behavior failed")) return 1;
    if (!expect(runDispatcherTuxMethodsTest(), "json rpc tux behavior failed")) return 1;
    if (!expect(runDispatcherWithoutFileServiceTest(), "json rpc dispatcher without file service behavior failed")) return 1;
    if (!expect(runDispatcherSetupCreateInitialAdminTest(), "json rpc setup behavior failed")) return 1;
    if (!expect(runDispatcherAuditMethodsTest(), "json rpc audit methods behavior failed")) return 1;

    return 0;
}

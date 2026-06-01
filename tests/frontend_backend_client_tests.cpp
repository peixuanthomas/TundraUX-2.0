#include "backend_client.hpp"
#include "backend_process.hpp"
#include "backend_runtime.hpp"
#include "json.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

class FakeTransport final : public tundraux::frontend::BackendLineTransport {
public:
    bool available = true;
    std::string nextResponse;
    std::vector<std::string> requests;

    bool requestLine(const std::string& line, std::string& response) override {
        requests.push_back(line);
        if (!available) {
            return false;
        }
        response = nextResponse;
        return true;
    }
};

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

const tundraux::backend::JsonValue::Object* parseRequestObject(
    const FakeTransport& transport,
    const std::string& label,
    tundraux::backend::JsonValue& parsedValue
) {
    if (!expect(!transport.requests.empty(), label + " should send request")) {
        return nullptr;
    }
    const auto parsed = tundraux::backend::parseJson(transport.requests.back());
    if (!expect(parsed.ok, label + " request should parse")) {
        return nullptr;
    }
    parsedValue = parsed.value;
    if (!expect(parsedValue.type() == tundraux::backend::JsonValue::Type::Object, label + " request should be object")) {
        return nullptr;
    }
    return &parsedValue.asObject();
}

bool expectRequestMethod(const FakeTransport& transport, const std::string& method, const std::string& label) {
    tundraux::backend::JsonValue parsed;
    const auto* object = parseRequestObject(transport, label, parsed);
    if (object == nullptr) {
        return false;
    }
    return expect(object->at("method").asString() == method, label + " method mismatch");
}

template <typename T>
bool expectInvalidResponse(const tundraux::frontend::ClientResult<T>& result, const std::string& label) {
    return expect(!result.ok, label + " should fail") &&
        expect(result.errorCode == "InvalidResponse", label + " error code mismatch") &&
        expect(result.message == "Invalid backend response.", label + " error message mismatch");
}

std::filesystem::path currentExecutableDirectory(const std::string& selfPath);

bool runStartGuestSessionTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"sessionId":"guest-1","user":{"name":"","type":"guest"}}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.startGuestSession();

    return expect(result.ok, "start guest should succeed") &&
        expect(result.value.sessionId == "guest-1", "start guest session id mismatch") &&
        expect(result.value.user.name.empty(), "start guest user name mismatch") &&
        expect(result.value.user.type == "guest", "start guest user type mismatch") &&
        expectRequestMethod(transport, "session.startGuestSession", "start guest");
}

bool runListUsersErrorTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","error":{"code":"PermissionDenied","message":"Admin access required."}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.listUsers("session-1");

    return expect(!result.ok, "list users should fail") &&
        expect(result.errorCode == "PermissionDenied", "list users error code mismatch") &&
        expect(result.message == "Admin access required.", "list users error message mismatch");
}

bool runReadFileSuccessTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"content":"hello\nworld"}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.readFile("session-1", "docs/note.txt");

    return expect(result.ok, "read file should succeed") &&
        expect(result.value == "hello\nworld", "read file content mismatch") &&
        expectRequestMethod(transport, "file.readFile", "read file");
}

bool runWhoamiMalformedResponseTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"user":{"name":"alice"}}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.whoami("session-1");

    return expect(!result.ok, "whoami malformed should fail") &&
        expect(result.errorCode == "InvalidResponse", "whoami malformed error code mismatch") &&
        expect(result.message == "Invalid backend response.", "whoami malformed error message mismatch");
}

bool runMissingResponseIdTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"result":{"content":"hello"}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.readFile("session-1", "docs/note.txt");

    return expectInvalidResponse(result, "missing response id");
}

bool runWrongTypeResponseIdTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":1,"result":{"content":"hello"}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.readFile("session-1", "docs/note.txt");

    return expectInvalidResponse(result, "wrong-type response id");
}

bool runMismatchedSuccessResponseIdTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"2","result":{"content":"hello"}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.readFile("session-1", "docs/note.txt");

    return expectInvalidResponse(result, "mismatched success response id");
}

bool runMismatchedErrorResponseIdTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"2","error":{"code":"PermissionDenied","message":"No."}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.listUsers("session-1");

    return expectInvalidResponse(result, "mismatched error response id");
}

bool runTransportFailureTest() {
    FakeTransport transport;
    transport.available = false;
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.readFile("session-1", "docs/note.txt");

    return expect(!result.ok, "transport failure should fail") &&
        expect(result.errorCode == "TransportError", "transport failure error code mismatch") &&
        expect(result.message == "Backend unavailable.", "transport failure message mismatch");
}

bool runWriteFileTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"ok":true}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.writeFile("session-1", "docs/note.txt", "updated");

    tundraux::backend::JsonValue parsed;
    const auto* request = parseRequestObject(transport, "write file", parsed);
    if (request == nullptr) {
        return false;
    }
    const auto& params = request->at("params").asObject();
    return expect(result.ok, "write file should succeed") &&
        expect(result.value, "write file result value mismatch") &&
        expect(request->at("method").asString() == "file.writeFile", "write file method mismatch") &&
        expect(params.at("path").asString() == "docs/note.txt", "write file path param mismatch") &&
        expect(params.at("content").asString() == "updated", "write file content param mismatch");
}

bool runListDirectoryTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"entries":[{"name":"note.txt","path":"docs/note.txt","type":"file","size":5}]}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.listDirectory("session-1", "docs");

    return expect(result.ok, "list directory should succeed") &&
        expect(result.value.size() == 1, "list directory entry count mismatch") &&
        expect(result.value[0].name == "note.txt", "list directory entry name mismatch") &&
        expect(result.value[0].path == "docs/note.txt", "list directory entry path mismatch") &&
        expect(result.value[0].type == "file", "list directory entry type mismatch") &&
        expect(result.value[0].size == 5ULL, "list directory entry size mismatch");
}

bool runFileMutationAndSearchMethodsTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"ok":true}})";
    tundraux::frontend::BackendClient client(transport);

    const auto deleteResult = client.deleteFile("session-1", "docs/old.txt");
    if (!expect(deleteResult.ok && deleteResult.value, "delete file should succeed") ||
        !expectRequestMethod(transport, "file.deleteFile", "delete file")) {
        return false;
    }

    transport.nextResponse = R"({"id":"2","result":{"ok":true}})";
    const auto renameResult = client.renameFile("session-1", "docs/old.txt", "docs/new.txt", false);
    if (!expect(renameResult.ok && renameResult.value, "rename file should succeed") ||
        !expectRequestMethod(transport, "file.renameFile", "rename file")) {
        return false;
    }

    transport.nextResponse = R"({"id":"3","result":{"ok":true}})";
    const auto copyResult = client.copyFile("session-1", "docs/new.txt", "docs/copy.txt", true);
    if (!expect(copyResult.ok && copyResult.value, "copy file should succeed") ||
        !expectRequestMethod(transport, "file.copyFile", "copy file")) {
        return false;
    }

    transport.nextResponse = R"({"id":"4","result":{"ok":true}})";
    const auto moveResult = client.moveFile("session-1", "docs/copy.txt", "archive/copy.txt", false);
    if (!expect(moveResult.ok && moveResult.value, "move file should succeed") ||
        !expectRequestMethod(transport, "file.moveFile", "move file")) {
        return false;
    }

    transport.nextResponse = R"({"id":"5","result":{"ok":true}})";
    const auto mkdirResult = client.createDirectory("session-1", "archive");
    if (!expect(mkdirResult.ok && mkdirResult.value, "create directory should succeed") ||
        !expectRequestMethod(transport, "file.createDirectory", "create directory")) {
        return false;
    }

    transport.nextResponse = R"({"id":"6","result":{"ok":true}})";
    const auto rmdirResult = client.removeDirectory("session-1", "archive", true);
    if (!expect(rmdirResult.ok && rmdirResult.value, "remove directory should succeed") ||
        !expectRequestMethod(transport, "file.removeDirectory", "remove directory")) {
        return false;
    }

    transport.nextResponse = R"({"id":"7","result":{"entries":[{"name":"copy.txt","path":"archive/copy.txt","type":"file","size":4}]}})";
    const auto searchResult = client.searchFiles("session-1", "archive", "copy");
    return expect(searchResult.ok, "file search should succeed") &&
        expect(searchResult.value.size() == 1, "file search entry count mismatch") &&
        expect(searchResult.value[0].path == "archive/copy.txt", "file search path mismatch") &&
        expectRequestMethod(transport, "file.search", "file search");
}

bool runTuxMethodsTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"entries":[{"name":"secret","path":"docs/secret","type":"file","size":12}]}})";
    tundraux::frontend::BackendClient client(transport);

    const auto listResult = client.listTux("session-1", "docs");
    if (!expect(listResult.ok, "tux list should succeed") ||
        !expect(listResult.value.size() == 1, "tux list entry count mismatch") ||
        !expectRequestMethod(transport, "tux.list", "tux list")) {
        return false;
    }

    transport.nextResponse = R"({"id":"2","result":{"ok":true}})";
    const auto createResult = client.createTux("session-1", "docs/secret", false);
    if (!expect(createResult.ok && createResult.value, "tux create should succeed") ||
        !expectRequestMethod(transport, "tux.create", "tux create")) {
        return false;
    }

    transport.nextResponse = R"({"id":"3","result":{"content":"hello","creator":"alice","lastEditor":"bob"}})";
    const auto readResult = client.readTux("session-1", "docs/secret");
    if (!expect(readResult.ok, "tux read should succeed") ||
        !expect(readResult.value.content == "hello", "tux content mismatch") ||
        !expect(readResult.value.creator == "alice", "tux creator mismatch") ||
        !expect(readResult.value.lastEditor == "bob", "tux last editor mismatch") ||
        !expectRequestMethod(transport, "tux.read", "tux read")) {
        return false;
    }

    transport.nextResponse = R"({"id":"4","result":{"ok":true}})";
    const auto writeResult = client.writeTux("session-1", "docs/secret", "updated");
    if (!expect(writeResult.ok && writeResult.value, "tux write should succeed") ||
        !expectRequestMethod(transport, "tux.write", "tux write")) {
        return false;
    }

    transport.nextResponse = R"({"id":"5","result":{"ok":true}})";
    const auto renameResult = client.renameTux("session-1", "docs/secret", "docs/renamed", false);
    if (!expect(renameResult.ok && renameResult.value, "tux rename should succeed") ||
        !expectRequestMethod(transport, "tux.rename", "tux rename")) {
        return false;
    }

    transport.nextResponse = R"({"id":"6","result":{"ok":true}})";
    const auto copyResult = client.copyTux("session-1", "docs/renamed", "docs/copy", true);
    if (!expect(copyResult.ok && copyResult.value, "tux copy should succeed") ||
        !expectRequestMethod(transport, "tux.copy", "tux copy")) {
        return false;
    }

    transport.nextResponse = R"({"id":"7","result":{"ok":true}})";
    const auto moveResult = client.moveTux("session-1", "docs/copy", "docs/moved", false);
    if (!expect(moveResult.ok && moveResult.value, "tux move should succeed") ||
        !expectRequestMethod(transport, "tux.move", "tux move")) {
        return false;
    }

    transport.nextResponse = R"({"id":"8","result":{"entries":[{"name":"moved","path":"docs/moved","type":"file","size":7}]}})";
    const auto searchResult = client.searchTux("session-1", "docs", "moved");
    if (!expect(searchResult.ok, "tux search should succeed") ||
        !expect(searchResult.value.size() == 1, "tux search entry count mismatch") ||
        !expectRequestMethod(transport, "tux.search", "tux search")) {
        return false;
    }

    transport.nextResponse = R"({"id":"9","result":{"ok":true}})";
    const auto deleteResult = client.deleteTux("session-1", "docs/moved");
    return expect(deleteResult.ok && deleteResult.value, "tux delete should succeed") &&
        expectRequestMethod(transport, "tux.delete", "tux delete");
}

bool runListDirectoryMaxSafeSizeTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"entries":[{"name":"large.bin","path":"docs/large.bin","type":"file","size":9007199254740991}]}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.listDirectory("session-1", "docs");

    return expect(result.ok, "list directory max safe size should succeed") &&
        expect(result.value.size() == 1, "list directory max safe size entry count mismatch") &&
        expect(result.value[0].size == 9007199254740991ULL, "list directory max safe size mismatch");
}

bool runListDirectoryUnsafeSizeTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"entries":[{"name":"large.bin","path":"docs/large.bin","type":"file","size":9007199254740992}]}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.listDirectory("session-1", "docs");

    return expectInvalidResponse(result, "list directory unsafe size");
}

bool runListDirectoryFractionalSizeTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"entries":[{"name":"note.txt","path":"docs/note.txt","type":"file","size":5.9}]}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.listDirectory("session-1", "docs");

    return expectInvalidResponse(result, "list directory fractional size");
}

bool runListDirectoryNegativeSizeTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"entries":[{"name":"note.txt","path":"docs/note.txt","type":"file","size":-1}]}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.listDirectory("session-1", "docs");

    return expectInvalidResponse(result, "list directory negative size");
}

bool runLoginTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"sessionId":"session-1","user":{"name":"alice","type":"admin"}}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.login("guest-1", "alice", "Secret1");

    return expect(result.ok, "login should succeed") &&
        expect(result.value.sessionId == "session-1", "login session id mismatch") &&
        expect(result.value.user.name == "alice", "login user name mismatch") &&
        expect(result.value.user.type == "admin", "login user type mismatch");
}

bool runLogoutTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"ok":true}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.logout("session-1");

    return expect(result.ok, "logout should succeed") &&
        expect(result.value, "logout result value mismatch") &&
        expectRequestMethod(transport, "session.logout", "logout");
}

bool runCurrentProfileTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"user":{"name":"alice","type":"admin","passwordHint":"alpha","failedCount":3}}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.currentProfile("session-1");
    tundraux::backend::JsonValue parsed;
    const auto* request = parseRequestObject(transport, "current profile", parsed);
    if (request == nullptr) {
        return false;
    }
    const auto& params = request->at("params").asObject();

    return expect(result.ok, "current profile should succeed") &&
        expect(result.value.name == "alice", "current profile name mismatch") &&
        expect(result.value.type == "admin", "current profile type mismatch") &&
        expect(result.value.passwordHint == "alpha", "current profile hint mismatch") &&
        expect(result.value.failedCount == 3, "current profile failed count mismatch") &&
        expect(request->at("method").asString() == "user.currentProfile", "current profile method mismatch") &&
        expect(params.at("sessionId").asString() == "session-1", "current profile sessionId param mismatch");
}

bool runUpdateOwnAccountTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"ok":true}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.updateOwnAccount("session-1", true, "Secret2", true, "new hint");

    tundraux::backend::JsonValue parsed;
    const auto* request = parseRequestObject(transport, "update own account", parsed);
    if (request == nullptr) {
        return false;
    }
    const auto& params = request->at("params").asObject();
    return expect(result.ok, "update own account should succeed") &&
        expect(result.value, "update own account result mismatch") &&
        expect(request->at("method").asString() == "user.updateOwnAccount", "update own account method mismatch") &&
        expect(params.at("sessionId").asString() == "session-1", "update own account sessionId param mismatch") &&
        expect(params.at("passwordProvided").asBoolean(), "update own account passwordProvided mismatch") &&
        expect(params.at("passwordHintProvided").asBoolean(), "update own account passwordHintProvided mismatch") &&
        expect(params.at("password").asString() == "Secret2", "update own account password mismatch") &&
        expect(params.at("passwordHint").asString() == "new hint", "update own account hint mismatch");
}

bool runUpdateOwnAccountWithoutOptionalFieldsTest() {
    FakeTransport transport;
    transport.nextResponse = R"({"id":"1","result":{"ok":true}})";
    tundraux::frontend::BackendClient client(transport);

    const auto result = client.updateOwnAccount("session-1", false, "ignored", false, "ignored");

    tundraux::backend::JsonValue parsed;
    const auto* request = parseRequestObject(transport, "update own account optional fields", parsed);
    if (request == nullptr) {
        return false;
    }
    const auto& params = request->at("params").asObject();
    return expect(result.ok, "update own account optional fields should succeed") &&
        expect(result.value, "update own account optional fields result mismatch") &&
        expect(params.at("sessionId").asString() == "session-1", "update own account optional fields sessionId param mismatch") &&
        expect(params.find("password") == params.end(), "update own account should omit password field") &&
        expect(params.find("passwordHint") == params.end(), "update own account should omit passwordHint field");
}

bool runRuntimeLegacyDirectTest(const std::string& selfPath) {
    tundraux::frontend::BackendRuntime runtime;
    tundraux::frontend::BackendRuntimeOptions options;
    options.legacyDirect = true;
    std::string error;

    const bool initialized = runtime.initialize(options, error);

    return expect(initialized, "legacy-direct runtime should initialize") &&
        expect(error.empty(), "legacy-direct runtime should not set error") &&
        expect(runtime.legacyDirect(), "legacy-direct runtime flag mismatch") &&
        expect(runtime.client() == nullptr, "legacy-direct runtime should not create client") &&
        expect(std::filesystem::path(runtime.filesRoot()) == currentExecutableDirectory(selfPath), "legacy-direct runtime files root mismatch") &&
        expect(runtime.sessionId().empty(), "legacy-direct runtime should not set session id");
}

bool runRuntimeMissingBackendPathTest() {
    tundraux::frontend::BackendRuntime runtime;
    tundraux::frontend::BackendRuntimeOptions options;
    const auto missingPath = std::filesystem::temp_directory_path() /
        ("tundraux_missing_backend_stdio_" + std::to_string(reinterpret_cast<std::uintptr_t>(&runtime))) /
        "tundraux_backend_stdio.exe";
    options.backendStdioPath = missingPath.string();
    std::string error;

    const bool initialized = runtime.initialize(options, error);

    return expect(!initialized, "missing backend runtime should fail") &&
        expect(!error.empty(), "missing backend runtime should set error") &&
        expect(!runtime.legacyDirect(), "missing backend runtime should not be legacy-direct") &&
        expect(runtime.client() == nullptr, "missing backend runtime should not create client") &&
        expect(runtime.filesRoot().empty(), "missing backend runtime should not keep files root") &&
        expect(runtime.sessionId().empty(), "missing backend runtime should not set session id");
}

bool runProcessResponseLineTooLongTest(const std::string& selfPath) {
    tundraux::frontend::BackendProcessTransport transport;
    if (!transport.start(selfPath, "too-long-response", "unused")) {
        return expect(false, "too-long process transport should start fake backend");
    }

    std::string response;
    const bool ok = transport.requestLine(R"({"id":"1","method":"test","params":{}})", response);
    transport.stop();

    constexpr std::size_t maxExpectedResponseBytes = 20ULL * 1024ULL * 1024ULL;
    return expect(!ok, "too-long process response should fail transport") &&
        expect(response.size() <= maxExpectedResponseBytes, "too-long process response should stay bounded");
}

std::filesystem::path currentExecutableDirectory(const std::string& selfPath) {
    std::error_code error;
    const auto executablePath = std::filesystem::weakly_canonical(std::filesystem::path(selfPath), error);
    if (!error) {
        return executablePath.parent_path();
    }
    return std::filesystem::absolute(std::filesystem::path(selfPath)).parent_path();
}

bool runRuntimeDefaultFilesRootIsExecutableDirectoryTest(const std::string& selfPath) {
    tundraux::frontend::BackendRuntime runtime;
    tundraux::frontend::BackendRuntimeOptions options;
    options.backendStdioPath = selfPath;
    options.userDataPath = "expect-executable-directory-root";
    std::string error;

    const bool initialized = runtime.initialize(options, error);
    runtime.shutdown();

    return expect(initialized, "runtime should pass executable directory as files root to backend");
}

bool runRuntimeDebugStartupUsesDebugSessionTest(const std::string& selfPath) {
    tundraux::frontend::BackendRuntime runtime;
    tundraux::frontend::BackendRuntimeOptions options;
    options.backendStdioPath = selfPath;
    options.userDataPath = "expect-debug-startup-session";
    options.startupUserType = "debug";
    options.startupUserName = "debug";
    std::string error;

    const bool initialized = runtime.initialize(options, error);
    const auto sessionId = runtime.sessionId();
    runtime.shutdown();

    return expect(initialized, "debug startup runtime should initialize") &&
        expect(sessionId == "debug-session", "debug startup runtime session id mismatch");
}

int runFakeBackendMode(int argc, char* argv[], const std::string& mode) {
    std::string request;
    if (!std::getline(std::cin, request)) {
        return 1;
    }

    if (mode == "too-long-response") {
        constexpr std::size_t totalBytes = 20ULL * 1024ULL * 1024ULL + 1ULL;
        const std::string chunk(64 * 1024, 'x');
        std::size_t remaining = totalBytes;
        while (remaining > 0) {
            const std::size_t toWrite = std::min(remaining, chunk.size());
            std::cout.write(chunk.data(), static_cast<std::streamsize>(toWrite));
            if (!std::cout) {
                return 1;
            }
            remaining -= toWrite;
        }
        std::cout.flush();
        return 0;
    }

    if (mode == "expect-executable-directory-root") {
        std::string filesRoot;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--files-root") {
                filesRoot = argv[i + 1];
                break;
            }
        }

        const std::string expected = currentExecutableDirectory(argv[0]).u8string();
        if (std::filesystem::path(filesRoot) != std::filesystem::path(expected)) {
            std::cout << R"({"id":"1","error":{"code":"InvalidFilesRoot","message":"files root mismatch"}})" << "\n";
            std::cout.flush();
            return 0;
        }

        std::cout << R"({"id":"1","result":{"sessionId":"test-session","user":{"name":"","type":"guest"}}})" << "\n";
        std::cout.flush();
        return 0;
    }

    if (mode == "expect-debug-startup-session") {
        std::string debugToken;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--debug-session-token") {
                debugToken = argv[i + 1];
                break;
            }
        }
        const auto parsed = tundraux::backend::parseJson(request);
        bool valid = parsed.ok &&
            parsed.value.type() == tundraux::backend::JsonValue::Type::Object;
        std::string method;
        std::string token;
        if (valid) {
            const auto& object = parsed.value.asObject();
            const auto methodIt = object.find("method");
            const auto paramsIt = object.find("params");
            valid = methodIt != object.end() &&
                methodIt->second.type() == tundraux::backend::JsonValue::Type::String &&
                paramsIt != object.end() &&
                paramsIt->second.type() == tundraux::backend::JsonValue::Type::Object;
            if (valid) {
                method = methodIt->second.asString();
                const auto& params = paramsIt->second.asObject();
                const auto tokenIt = params.find("token");
                valid = tokenIt != params.end() &&
                    tokenIt->second.type() == tundraux::backend::JsonValue::Type::String;
                if (valid) {
                    token = tokenIt->second.asString();
                }
            }
        }

        if (debugToken.empty() || method != "session.startDebugSession" || token != debugToken) {
            std::cout << R"({"id":"1","error":{"code":"PermissionDenied","message":"debug startup mismatch"}})" << "\n";
            std::cout.flush();
            return 0;
        }

        std::cout << R"({"id":"1","result":{"sessionId":"debug-session","user":{"name":"debug","type":"debug"}}})" << "\n";
        std::cout.flush();
        return 0;
    }

    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--user-data") {
            return runFakeBackendMode(argc, argv, argv[i + 1]);
        }
    }

    if (!runStartGuestSessionTest()) return 1;
    if (!runListUsersErrorTest()) return 1;
    if (!runReadFileSuccessTest()) return 1;
    if (!runWhoamiMalformedResponseTest()) return 1;
    if (!runMissingResponseIdTest()) return 1;
    if (!runWrongTypeResponseIdTest()) return 1;
    if (!runMismatchedSuccessResponseIdTest()) return 1;
    if (!runMismatchedErrorResponseIdTest()) return 1;
    if (!runTransportFailureTest()) return 1;
    if (!runWriteFileTest()) return 1;
    if (!runListDirectoryTest()) return 1;
    if (!runFileMutationAndSearchMethodsTest()) return 1;
    if (!runTuxMethodsTest()) return 1;
    if (!runListDirectoryMaxSafeSizeTest()) return 1;
    if (!runListDirectoryUnsafeSizeTest()) return 1;
    if (!runListDirectoryFractionalSizeTest()) return 1;
    if (!runListDirectoryNegativeSizeTest()) return 1;
    if (!runLoginTest()) return 1;
    if (!runLogoutTest()) return 1;
    if (!runCurrentProfileTest()) return 1;
    if (!runUpdateOwnAccountTest()) return 1;
    if (!runUpdateOwnAccountWithoutOptionalFieldsTest()) return 1;
    if (!runRuntimeLegacyDirectTest(argv[0])) return 1;
    if (!runRuntimeMissingBackendPathTest()) return 1;
    if (!runProcessResponseLineTooLongTest(argv[0])) return 1;
    if (!runRuntimeDefaultFilesRootIsExecutableDirectoryTest(argv[0])) return 1;
    if (!runRuntimeDebugStartupUsesDebugSessionTest(argv[0])) return 1;
    return 0;
}

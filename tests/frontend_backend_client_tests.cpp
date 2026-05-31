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

bool runRuntimeLegacyDirectTest() {
    tundraux::frontend::BackendRuntime runtime;
    tundraux::frontend::BackendRuntimeOptions options;
    options.legacyDirect = true;
    std::string error;

    const bool initialized = runtime.initialize(options, error);

    return expect(initialized, "legacy-direct runtime should initialize") &&
        expect(error.empty(), "legacy-direct runtime should not set error") &&
        expect(runtime.legacyDirect(), "legacy-direct runtime flag mismatch") &&
        expect(runtime.client() == nullptr, "legacy-direct runtime should not create client") &&
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

int runFakeBackendMode(const std::string& mode) {
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

    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--user-data") {
            return runFakeBackendMode(argv[i + 1]);
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
    if (!runListDirectoryMaxSafeSizeTest()) return 1;
    if (!runListDirectoryUnsafeSizeTest()) return 1;
    if (!runListDirectoryFractionalSizeTest()) return 1;
    if (!runListDirectoryNegativeSizeTest()) return 1;
    if (!runLoginTest()) return 1;
    if (!runLogoutTest()) return 1;
    if (!runRuntimeLegacyDirectTest()) return 1;
    if (!runRuntimeMissingBackendPathTest()) return 1;
    if (!runProcessResponseLineTooLongTest(argv[0])) return 1;
    return 0;
}

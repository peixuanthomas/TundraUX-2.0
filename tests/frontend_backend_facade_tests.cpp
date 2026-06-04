#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "protocol_json.hpp"

#define private public
#include "backend_runtime.hpp"
#include "backend_facade.hpp"
#undef private

namespace {

class FakeTransport final : public tundraux::frontend::BackendLineTransport {
public:
    bool available = true;
    std::vector<std::string> responses;
    std::vector<std::string> requests;

    bool requestLine(const std::string& line, std::string& response) override {
        requests.push_back(line);
        if (!available) {
            return false;
        }
        if (responseIndex >= responses.size()) {
            return false;
        }
        response = responses[responseIndex++];
        return true;
    }

private:
    std::size_t responseIndex = 0;
};

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

const tundraux::protocol::JsonValue::Object* parseRequestObject(
    const FakeTransport& transport,
    std::size_t index,
    tundraux::protocol::JsonValue& parsed
) {
    if (index >= transport.requests.size()) {
        return nullptr;
    }
    const auto parsedRequest = tundraux::protocol::parseJson(transport.requests[index]);
    if (!parsedRequest.ok || parsedRequest.value.type() != tundraux::protocol::JsonValue::Type::Object) {
        return nullptr;
    }
    parsed = parsedRequest.value;
    return &parsed.asObject();
}

void configureFacadeRuntime(
    tundraux::frontend::BackendRuntime& runtime,
    FakeTransport& transport,
    const std::string& sessionId = ""
) {
    runtime.client_ = std::make_unique<tundraux::frontend::BackendClient>(transport);
    runtime.legacyDirect_ = false;
    runtime.sessionId_ = sessionId;
    runtime.filesRoot_ = "test-files-root";
}

bool runFacadeActiveModeChecks() {
    const bool inactiveWorks = [] {
        tundraux::frontend::BackendRuntime runtime;
        tundraux::frontend::BackendFacade facade(runtime);
        std::string message;
        return expect(!facade.active(), "inactive runtime should not be active") &&
            expect(!facade.ensureSession(message), "inactive runtime should fail ensureSession") &&
            expect(message == "Backend unavailable.", "inactive runtime should set backend unavailable message");
    }();

    const bool legacyDirectWorks = [] {
        FakeTransport transport;
        transport.responses = {R"({"id":"1","result":{"sessionId":"guest-1","user":{"name":"","type":"guest"}}})"};
        tundraux::frontend::BackendRuntime runtime;
        runtime.legacyDirect_ = true;
        runtime.client_ = std::make_unique<tundraux::frontend::BackendClient>(transport);
        runtime.sessionId_ = "";
        tundraux::frontend::BackendFacade facade(runtime);
        std::string message;
        return expect(!facade.active(), "legacy-direct runtime should not be active") &&
            expect(!facade.ensureSession(message), "legacy-direct runtime should fail ensureSession") &&
            expect(message == "Backend unavailable.", "legacy-direct runtime should set backend unavailable message");
    }();

    return inactiveWorks && legacyDirectWorks;
}

bool runFacadeEnsureSessionStartsGuestSession() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"sessionId":"guest-1","user":{"name":"","type":"guest"}}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "");
    tundraux::frontend::BackendFacade facade(runtime);

    std::string message;
    const bool ok = facade.ensureSession(message);

    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }

    return expect(ok, "ensure session should start guest session") &&
        expect(requestObject->at("method").asString() == "session.startGuestSession", "ensure session method mismatch") &&
        expect(runtime.sessionId_ == "guest-1", "runtime session id mismatch after ensure session") &&
        expect(message.empty(), "ensure session message should be empty");
}

bool runFacadeRefreshProfileConvertsUserWithoutPassword() {
    FakeTransport transport;
    transport.responses = {
        R"({"id":"1","result":{"user":{"name":"alice","type":"admin","passwordHint":"alpha","failedCount":3}}})"
    };

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-1");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto profile = facade.refreshProfile();
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }

    return expect(profile.ok, "refresh profile should succeed") &&
        expect(profile.value.type == "admin", "profile type mismatch") &&
        expect(profile.value.name == "alice", "profile name mismatch") &&
        expect(profile.value.passwordHint == "alpha", "profile password hint mismatch") &&
        expect(profile.value.failedCount == 3, "profile failed count mismatch") &&
        expect(profile.errorCode.empty(), "refresh profile error code should be empty on success") &&
        expect(requestObject->at("method").asString() == "user.currentProfile", "refresh profile method mismatch") &&
        expect(requestObject->at("params").asObject().at("sessionId").asString() == "session-1", "refresh profile session id mismatch");
}

bool runFacadeSetStrictModeSendsRequestAndReturnsMessage() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","error":{"code":"PermissionDenied","message":"not allowed"}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-1");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.setStrictMode(true);
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(!result.ok, "set strict mode should fail") &&
        expect(result.errorCode == "PermissionDenied", "set strict mode error code mismatch") &&
        expect(result.message == "not allowed", "set strict mode message mismatch") &&
        expect(requestObject->at("method").asString() == "user.setStrictMode", "set strict mode method mismatch") &&
        expect(params.at("sessionId").asString() == "session-1", "set strict mode session mismatch") &&
        expect(params.at("enabled").asBoolean(), "set strict mode enabled mismatch");
}

bool runFacadeSetStrictModeSessionExpiredRecovery() {
    FakeTransport transport;
    transport.responses = {
        R"({"id":"1","error":{"code":"SessionExpired","message":"session expired"}})",
        R"({"id":"2","result":{"sessionId":"guest-2","user":{"name":"","type":"guest"}}})"
    };

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "expired-session");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.setStrictMode(true);
    tundraux::protocol::JsonValue requestOne;
    tundraux::protocol::JsonValue requestTwo;

    const auto* firstRequest = parseRequestObject(transport, 0, requestOne);
    const auto* secondRequest = parseRequestObject(transport, 1, requestTwo);
    if (firstRequest == nullptr || secondRequest == nullptr) {
        return false;
    }

    return expect(!result.ok, "set strict mode should fail for session expired") &&
        expect(result.errorCode == "SessionExpired", "set strict mode session expired code mismatch") &&
        expect(runtime.sessionId_ == "guest-2", "session should recover to guest session") &&
        expect(firstRequest->at("method").asString() == "user.setStrictMode", "first request should be user.setStrictMode") &&
        expect(firstRequest->at("params").asObject().at("sessionId").asString() == "expired-session", "first request should use expired session id") &&
        expect(secondRequest->at("method").asString() == "session.startGuestSession", "recovery should start guest session");
}

bool runFacadeLogEventSendsSession() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"ok":true}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-2");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.logEvent("shell", "entered command");
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "log event should succeed") &&
        expect(requestObject->at("method").asString() == "audit.logEvent", "log event method mismatch") &&
        expect(params.at("sessionId").asString() == "session-2", "log event session mismatch") &&
        expect(params.at("category").asString() == "shell", "log event category mismatch") &&
        expect(params.at("detail").asString() == "entered command", "log event detail mismatch");
}

bool runFacadeLogKeyPressSendsSession() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"ok":true}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-3");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.logKeyPress("x", true);
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "log key press should succeed") &&
        expect(result.errorCode.empty(), "log key press error code should be empty on success") &&
        expect(requestObject->at("method").asString() == "audit.logKeyPress", "log key press method mismatch") &&
        expect(params.at("sessionId").asString() == "session-3", "log key press session mismatch") &&
        expect(params.at("key").asString() == "x", "log key press key mismatch") &&
        expect(params.at("sensitive").asBoolean(), "log key press sensitive mismatch");
}

bool runFacadeGetStrictModeSendsRequest() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"enabled":true}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-4");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.getStrictMode();
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "get strict mode should succeed") &&
        expect(result.value, "get strict mode should return true") &&
        expect(result.errorCode.empty(), "get strict mode error code should be empty on success") &&
        expect(requestObject->at("method").asString() == "user.getStrictMode", "get strict mode method mismatch") &&
        expect(params.at("sessionId").asString() == "session-4", "get strict mode session mismatch");
}

bool runFacadeCreateInitialAdminSendsSetupRequest() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"ok":true}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "setup-session");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.createInitialAdmin("admin", "Secret1", "primary");
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "create initial admin should succeed") &&
        expect(result.errorCode.empty(), "create initial admin error code should be empty on success") &&
        expect(requestObject->at("method").asString() == "setup.createInitialAdmin", "create initial admin method mismatch") &&
        expect(params.at("sessionId").asString() == "setup-session", "create initial admin session mismatch") &&
        expect(params.at("username").asString() == "admin", "create initial admin username mismatch") &&
        expect(params.at("password").asString() == "Secret1", "create initial admin password mismatch") &&
        expect(params.at("passwordHint").asString() == "primary", "create initial admin password hint mismatch");
}

bool facadeLoginSendsSessionLoginAndStoresSession() {
    FakeTransport transport;
    transport.responses = {
        R"({"id":"1","result":{"sessionId":"login-session","user":{"name":"alice","type":"admin","passwordHint":"alpha","failedCount":2}}})"
    };

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "guest-session");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.login("alice", "Secret1");
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "login should succeed") &&
        expect(result.value.type == "admin", "login user type mismatch") &&
        expect(result.value.name == "alice", "login username mismatch") &&
        expect(result.value.passwordHint == "alpha", "login password hint mismatch") &&
        expect(result.value.failedCount == 2, "login failed count mismatch") &&
        expect(result.errorCode.empty(), "login error code should be empty on success") &&
        expect(runtime.sessionId_ == "login-session", "login should store returned session id") &&
        expect(requestObject->at("method").asString() == "session.login", "login method mismatch") &&
        expect(params.at("sessionId").asString() == "guest-session", "login session id mismatch") &&
        expect(params.at("username").asString() == "alice", "login username param mismatch") &&
        expect(params.at("password").asString() == "Secret1", "login password param mismatch");
}

bool facadeListUsersUsesCurrentSession() {
    FakeTransport transport;
    transport.responses = {
        R"({"id":"1","result":{"users":[{"name":"alice","type":"admin","passwordHint":"alpha","failedCount":0},{"name":"bob","type":"user","passwordHint":"beta","failedCount":4}]}})"
    };

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-users");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.listUsers();
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "list users should succeed") &&
        expect(result.value.size() == 2, "list users count mismatch") &&
        expect(result.value[0].name == "alice", "list users first name mismatch") &&
        expect(result.value[0].type == "admin", "list users first type mismatch") &&
        expect(result.value[0].passwordHint == "alpha", "list users first password hint mismatch") &&
        expect(result.value[1].name == "bob", "list users second name mismatch") &&
        expect(result.value[1].type == "user", "list users second type mismatch") &&
        expect(result.value[1].passwordHint == "beta", "list users second password hint mismatch") &&
        expect(result.value[1].failedCount == 4, "list users second failed count mismatch") &&
        expect(requestObject->at("method").asString() == "user.listUsers", "list users method mismatch") &&
        expect(params.at("sessionId").asString() == "session-users", "list users session mismatch");
}

bool facadeUpdateOwnAccountUsesCurrentSession() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","error":{"code":"ValidationError","message":"password too weak"}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-own");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.updateOwnAccount(true, "Secret2", true, "new hint");
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(!result.ok, "update own account should preserve backend failure") &&
        expect(result.errorCode == "ValidationError", "update own account error code mismatch") &&
        expect(result.message == "password too weak", "update own account message mismatch") &&
        expect(requestObject->at("method").asString() == "user.updateOwnAccount", "update own account method mismatch") &&
        expect(params.at("sessionId").asString() == "session-own", "update own account session mismatch") &&
        expect(params.at("passwordProvided").asBoolean(), "update own account passwordProvided mismatch") &&
        expect(params.at("password").asString() == "Secret2", "update own account password mismatch") &&
        expect(params.at("passwordHintProvided").asBoolean(), "update own account passwordHintProvided mismatch") &&
        expect(params.at("passwordHint").asString() == "new hint", "update own account password hint mismatch");
}

bool facadeLogoutUsesCurrentSessionAndClearsRuntimeSession() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"ok":true}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-logout");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.logout();
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "logout should succeed") &&
        expect(result.errorCode.empty(), "logout error code should be empty on success") &&
        expect(result.message.empty(), "logout message should be empty on success") &&
        expect(runtime.sessionId_.empty(), "logout should clear runtime session id") &&
        expect(requestObject->at("method").asString() == "session.logout", "logout method mismatch") &&
        expect(params.at("sessionId").asString() == "session-logout", "logout session mismatch");
}

bool facadeNewSessionMethodsRecoverGuestOnSessionExpired() {
    {
        FakeTransport transport;
        transport.responses = {
            R"({"id":"1","error":{"code":"SessionExpired","message":"expired login"}})",
            R"({"id":"2","result":{"sessionId":"guest-after-login","user":{"name":"","type":"guest"}}})"
        };
        tundraux::frontend::BackendRuntime runtime;
        configureFacadeRuntime(runtime, transport, "expired-login");
        tundraux::frontend::BackendFacade facade(runtime);

        const auto result = facade.login("alice", "Secret1");
        tundraux::protocol::JsonValue recoveryRequest;
        const auto* requestObject = parseRequestObject(transport, 1, recoveryRequest);
        if (requestObject == nullptr) {
            return false;
        }
        if (!expect(!result.ok, "expired login should fail") ||
            !expect(result.errorCode == "SessionExpired", "expired login code mismatch") ||
            !expect(runtime.sessionId_ == "guest-after-login", "expired login should recover guest session") ||
            !expect(requestObject->at("method").asString() == "session.startGuestSession", "expired login recovery method mismatch")) {
            return false;
        }
    }

    {
        FakeTransport transport;
        transport.responses = {
            R"({"id":"1","error":{"code":"SessionExpired","message":"expired logout"}})",
            R"({"id":"2","result":{"sessionId":"guest-after-logout","user":{"name":"","type":"guest"}}})"
        };
        tundraux::frontend::BackendRuntime runtime;
        configureFacadeRuntime(runtime, transport, "expired-logout");
        tundraux::frontend::BackendFacade facade(runtime);

        const auto result = facade.logout();
        if (!expect(!result.ok, "expired logout should fail") ||
            !expect(result.errorCode == "SessionExpired", "expired logout code mismatch") ||
            !expect(runtime.sessionId_ == "guest-after-logout", "expired logout should recover guest session")) {
            return false;
        }
    }

    {
        FakeTransport transport;
        transport.responses = {
            R"({"id":"1","error":{"code":"SessionExpired","message":"expired list"}})",
            R"({"id":"2","result":{"sessionId":"guest-after-list","user":{"name":"","type":"guest"}}})"
        };
        tundraux::frontend::BackendRuntime runtime;
        configureFacadeRuntime(runtime, transport, "expired-list");
        tundraux::frontend::BackendFacade facade(runtime);

        const auto result = facade.listUsers();
        if (!expect(!result.ok, "expired list users should fail") ||
            !expect(result.errorCode == "SessionExpired", "expired list users code mismatch") ||
            !expect(runtime.sessionId_ == "guest-after-list", "expired list users should recover guest session")) {
            return false;
        }
    }

    {
        FakeTransport transport;
        transport.responses = {
            R"({"id":"1","error":{"code":"SessionExpired","message":"expired update"}})",
            R"({"id":"2","result":{"sessionId":"guest-after-update","user":{"name":"","type":"guest"}}})"
        };
        tundraux::frontend::BackendRuntime runtime;
        configureFacadeRuntime(runtime, transport, "expired-update");
        tundraux::frontend::BackendFacade facade(runtime);

        const auto result = facade.updateOwnAccount(false, "", false, "");
        if (!expect(!result.ok, "expired update own account should fail") ||
            !expect(result.errorCode == "SessionExpired", "expired update own account code mismatch") ||
            !expect(runtime.sessionId_ == "guest-after-update", "expired update own account should recover guest session")) {
            return false;
        }
    }

    return true;
}

bool facadeDebugForceLoginStoresReturnedSession() {
    FakeTransport transport;
    transport.responses = {
        R"({"id":"1","result":{"session":{"sessionId":"debug-login-session","user":{"name":"alice","type":"admin","passwordHint":"alpha","failedCount":0}}}})"
    };

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "debug-session");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.debugForceLogin("alice");
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "debug force-login should succeed") &&
        expect(result.value.name == "alice", "debug force-login user mismatch") &&
        expect(result.value.type == "admin", "debug force-login type mismatch") &&
        expect(runtime.sessionId_ == "debug-login-session", "debug force-login should store returned session") &&
        expect(requestObject->at("method").asString() == "debug.forceLogin", "debug force-login method mismatch") &&
        expect(params.at("sessionId").asString() == "debug-session", "debug force-login session mismatch") &&
        expect(params.at("username").asString() == "alice", "debug force-login username mismatch");
}

bool runFacadeReadTlogSendsRequest() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"lines":["a","b","c"]}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-5");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.readTlog("audit.tlog");
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "read tlog should succeed") &&
        expect(result.value.size() == 3, "read tlog line count mismatch") &&
        expect(result.value[1] == "b", "read tlog second line mismatch") &&
        expect(requestObject->at("method").asString() == "audit.readTlog", "read tlog method mismatch") &&
        expect(params.at("sessionId").asString() == "session-5", "read tlog session mismatch") &&
        expect(params.at("path").asString() == "audit.tlog", "read tlog path mismatch");
}

bool runFacadeExportTlogSendsRequest() {
    FakeTransport transport;
    transport.responses = {R"({"id":"1","result":{"content":"plaintext"}})"};

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "session-6");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.exportTlog("audit.tlog");
    tundraux::protocol::JsonValue request;
    const auto* requestObject = parseRequestObject(transport, 0, request);
    if (requestObject == nullptr) {
        return false;
    }
    const auto& params = requestObject->at("params").asObject();

    return expect(result.ok, "export tlog should succeed") &&
        expect(result.value == "plaintext", "export tlog content mismatch") &&
        expect(requestObject->at("method").asString() == "audit.exportTlog", "export tlog method mismatch") &&
        expect(params.at("sessionId").asString() == "session-6", "export tlog session mismatch") &&
        expect(params.at("path").asString() == "audit.tlog", "export tlog path mismatch");
}

bool runFacadeSessionExpiredTriggersGuestRecovery() {
    FakeTransport transport;
    transport.responses = {
        R"({"id":"1","error":{"code":"SessionExpired","message":"session expired"}})",
        R"({"id":"2","result":{"sessionId":"guest-2","user":{"name":"","type":"guest"}}})"
    };

    tundraux::frontend::BackendRuntime runtime;
    configureFacadeRuntime(runtime, transport, "expired-session");
    tundraux::frontend::BackendFacade facade(runtime);

    const auto result = facade.refreshProfile();
    tundraux::protocol::JsonValue requestOne;
    tundraux::protocol::JsonValue requestTwo;

    const auto* firstRequest = parseRequestObject(transport, 0, requestOne);
    const auto* secondRequest = parseRequestObject(transport, 1, requestTwo);
    if (firstRequest == nullptr || secondRequest == nullptr) {
        return false;
    }

    return expect(!result.ok, "refresh profile should fail for session expired") &&
        expect(result.errorCode == "SessionExpired", "refresh profile session expired code mismatch") &&
        expect(runtime.sessionId_ == "guest-2", "session should recover to guest session") &&
        expect(firstRequest->at("method").asString() == "user.currentProfile", "first request should be user.currentProfile") &&
        expect(firstRequest->at("params").asObject().at("sessionId").asString() == "expired-session", "first request should use expired session id") &&
        expect(secondRequest->at("method").asString() == "session.startGuestSession", "recovery should start guest session");
}

} // namespace

int main() {
    if (!runFacadeActiveModeChecks()) {
        return 1;
    }
    if (!runFacadeEnsureSessionStartsGuestSession()) {
        return 1;
    }
    if (!runFacadeRefreshProfileConvertsUserWithoutPassword()) {
        return 1;
    }
    if (!runFacadeSetStrictModeSendsRequestAndReturnsMessage()) {
        return 1;
    }
    if (!runFacadeSetStrictModeSessionExpiredRecovery()) {
        return 1;
    }
    if (!runFacadeLogEventSendsSession()) {
        return 1;
    }
    if (!runFacadeLogKeyPressSendsSession()) {
        return 1;
    }
    if (!runFacadeGetStrictModeSendsRequest()) {
        return 1;
    }
    if (!runFacadeCreateInitialAdminSendsSetupRequest()) {
        return 1;
    }
    if (!facadeLoginSendsSessionLoginAndStoresSession()) {
        return 1;
    }
    if (!facadeListUsersUsesCurrentSession()) {
        return 1;
    }
    if (!facadeUpdateOwnAccountUsesCurrentSession()) {
        return 1;
    }
    if (!facadeLogoutUsesCurrentSessionAndClearsRuntimeSession()) {
        return 1;
    }
    if (!facadeNewSessionMethodsRecoverGuestOnSessionExpired()) {
        return 1;
    }
    if (!facadeDebugForceLoginStoresReturnedSession()) {
        return 1;
    }
    if (!runFacadeReadTlogSendsRequest()) {
        return 1;
    }
    if (!runFacadeExportTlogSendsRequest()) {
        return 1;
    }
    if (!runFacadeSessionExpiredTriggersGuestRecovery()) {
        return 1;
    }
    return 0;
}


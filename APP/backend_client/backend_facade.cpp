#include "backend_facade.hpp"

#include <string>
#include <utility>
#include <vector>

namespace tundraux::frontend {

ShellUser shellUserFromFrontendUser(const FrontendUser& user) {
    return {
        user.type,
        user.name,
        user.passwordHint,
        user.failedCount
    };
}

namespace {

std::string defaultFailureMessage(const std::string& fallback, const std::string& backendMessage) {
    return backendMessage.empty() ? fallback : backendMessage;
}

bool startGuestSession(BackendRuntime& runtime, std::string& message) {
    auto* client = runtime.client();
    if (client == nullptr) {
        message = "Backend unavailable.";
        return false;
    }

    const auto guestSession = client->startGuestSession();
    if (!guestSession.ok) {
        message = defaultFailureMessage("Unable to start backend session.", guestSession.message);
        return false;
    }

    runtime.setSessionId(guestSession.value.sessionId);
    return true;
}

bool recoverGuestSession(BackendRuntime& runtime, std::string& message) {
    runtime.setSessionId("");
    return startGuestSession(runtime, message);
}

} // namespace

BackendFacade::BackendFacade(BackendRuntime& runtime) : runtime_(runtime) {}

bool BackendFacade::active() const {
    return runtime_.client() != nullptr;
}

bool BackendFacade::ensureSession(std::string& message) {
    if (!active()) {
        message = "Backend unavailable.";
        return false;
    }

    if (!runtime_.sessionId().empty()) {
        return true;
    }

    return startGuestSession(runtime_, message);
}

ClientResult<ShellUser> BackendFacade::login(const std::string& username, const std::string& password) {
    std::string message;
    if (!ensureSession(message)) {
        return ClientResult<ShellUser>{false, {}, "TransportError", message};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return ClientResult<ShellUser>{false, {}, "TransportError", "Backend unavailable."};
    }

    const auto result = client->login(runtime_.sessionId(), username, password);
    if (!result.ok) {
        if (result.errorCode == "SessionExpired") {
            std::string recoveryMessage;
            (void)recoverGuestSession(runtime_, recoveryMessage);
        }
        return ClientResult<ShellUser>{
            false,
            {},
            result.errorCode,
            defaultFailureMessage("Unable to login.", result.message)
        };
    }

    runtime_.setSessionId(result.value.sessionId);
    return ClientResult<ShellUser>{
        true,
        shellUserFromFrontendUser(result.value.user),
        {},
        {}
    };
}

FacadeResult BackendFacade::logout() {
    std::string message;
    if (!ensureSession(message)) {
        return FacadeResult{false, message, "TransportError"};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return FacadeResult{false, "Backend unavailable.", "TransportError"};
    }

    const auto result = client->logout(runtime_.sessionId());
    if (result.ok) {
        runtime_.setSessionId("");
    } else if (result.errorCode == "SessionExpired") {
        std::string recoveryMessage;
        (void)recoverGuestSession(runtime_, recoveryMessage);
    }

    return FacadeResult{
        result.ok,
        result.message,
        result.errorCode
    };
}

ClientResult<std::vector<ShellUser>> BackendFacade::listUsers() {
    std::string message;
    if (!ensureSession(message)) {
        return ClientResult<std::vector<ShellUser>>{false, {}, "TransportError", message};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return ClientResult<std::vector<ShellUser>>{false, {}, "TransportError", "Backend unavailable."};
    }

    const auto result = client->listUsers(runtime_.sessionId());
    if (!result.ok) {
        if (result.errorCode == "SessionExpired") {
            std::string recoveryMessage;
            (void)recoverGuestSession(runtime_, recoveryMessage);
        }
        return ClientResult<std::vector<ShellUser>>{
            false,
            {},
            result.errorCode,
            defaultFailureMessage("Unable to list users.", result.message)
        };
    }

    std::vector<ShellUser> users;
    users.reserve(result.value.size());
    for (const auto& user : result.value) {
        users.push_back(shellUserFromFrontendUser(user));
    }

    return ClientResult<std::vector<ShellUser>>{
        true,
        std::move(users),
        {},
        {}
    };
}

FacadeResult BackendFacade::updateOwnAccount(
    bool passwordProvided,
    const std::string& password,
    bool passwordHintProvided,
    const std::string& passwordHint
) {
    std::string message;
    if (!ensureSession(message)) {
        return FacadeResult{false, message, "TransportError"};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return FacadeResult{false, "Backend unavailable.", "TransportError"};
    }

    const auto result = client->updateOwnAccount(
        runtime_.sessionId(),
        passwordProvided,
        password,
        passwordHintProvided,
        passwordHint
    );
    if (!result.ok && result.errorCode == "SessionExpired") {
        std::string recoveryMessage;
        (void)recoverGuestSession(runtime_, recoveryMessage);
    }

    return FacadeResult{
        result.ok,
        result.message,
        result.errorCode
    };
}

ClientResult<ShellUser> BackendFacade::debugForceLogin(const std::string& username) {
    std::string message;
    if (!ensureSession(message)) {
        return ClientResult<ShellUser>{false, {}, "TransportError", message};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return ClientResult<ShellUser>{false, {}, "TransportError", "Backend unavailable."};
    }

    const auto result = client->debugForceLogin(runtime_.sessionId(), username);
    if (!result.ok) {
        if (result.errorCode == "SessionExpired") {
            std::string recoveryMessage;
            (void)recoverGuestSession(runtime_, recoveryMessage);
        }
        return ClientResult<ShellUser>{
            false,
            {},
            result.errorCode,
            defaultFailureMessage("Debug force-login failed.", result.message)
        };
    }

    runtime_.setSessionId(result.value.sessionId);
    return ClientResult<ShellUser>{
        true,
        shellUserFromFrontendUser(result.value.user),
        {},
        {}
    };
}

ClientResult<ShellUser> BackendFacade::refreshProfile() {
    std::string message;
    if (!ensureSession(message)) {
        return ClientResult<ShellUser>{false, {}, "TransportError", message};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return ClientResult<ShellUser>{false, {}, "TransportError", "Backend unavailable."};
    }

    const auto profileResult = client->currentProfile(runtime_.sessionId());
    if (!profileResult.ok) {
        if (profileResult.errorCode == "SessionExpired") {
            std::string recoveryMessage;
            (void)recoverGuestSession(runtime_, recoveryMessage);
        }
        return ClientResult<ShellUser>{
            false,
            {},
            profileResult.errorCode,
            defaultFailureMessage("Unable to query backend profile.", profileResult.message)
        };
    }

    return ClientResult<ShellUser>{
        true,
        shellUserFromFrontendUser(profileResult.value),
        {},
        {}
    };
}

FacadeResult BackendFacade::logEvent(const std::string& category, const std::string& detail) {
    std::string message;
    if (!ensureSession(message)) {
        return FacadeResult{false, message, "TransportError"};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return FacadeResult{false, "Backend unavailable.", "TransportError"};
    }

    const auto result = client->logAuditEvent(runtime_.sessionId(), category, detail);
    if (!result.ok && result.errorCode == "SessionExpired") {
        std::string ignored;
        (void)recoverGuestSession(runtime_, ignored);
        return FacadeResult{false, defaultFailureMessage("Backend session expired.", result.message), result.errorCode};
    }

    return FacadeResult{
        result.ok,
        result.ok ? std::string{} : defaultFailureMessage("Unable to log audit event.", result.message),
        result.errorCode
    };
}

FacadeResult BackendFacade::logKeyPress(const std::string& key, bool sensitive) {
    std::string message;
    if (!ensureSession(message)) {
        return FacadeResult{false, message, "TransportError"};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return FacadeResult{false, "Backend unavailable.", "TransportError"};
    }

    const auto result = client->logAuditKeyPress(runtime_.sessionId(), key, sensitive);
    if (!result.ok && result.errorCode == "SessionExpired") {
        std::string ignored;
        (void)recoverGuestSession(runtime_, ignored);
        return FacadeResult{false, defaultFailureMessage("Backend session expired.", result.message), result.errorCode};
    }

    return FacadeResult{
        result.ok,
        result.ok ? std::string{} : defaultFailureMessage("Unable to log keypress.", result.message),
        result.errorCode
    };
}

ClientResult<bool> BackendFacade::getStrictMode() {
    std::string message;
    if (!ensureSession(message)) {
        return ClientResult<bool>{false, false, "TransportError", message};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return ClientResult<bool>{false, false, "TransportError", "Backend unavailable."};
    }

    const auto strictResult = client->getStrictMode(runtime_.sessionId());
    if (!strictResult.ok && strictResult.errorCode == "SessionExpired") {
        std::string ignored;
        (void)recoverGuestSession(runtime_, ignored);
        return ClientResult<bool>{
            false,
            false,
            strictResult.errorCode,
            defaultFailureMessage("Backend session expired.", strictResult.message)
        };
    }

    return strictResult.ok ? ClientResult<bool>{true, strictResult.value, {}, {}}
                          : ClientResult<bool>{false, false, strictResult.errorCode, strictResult.message};
}

FacadeResult BackendFacade::setStrictMode(bool enabled) {
    std::string message;
    if (!ensureSession(message)) {
        return FacadeResult{false, message, "TransportError"};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return FacadeResult{false, "Backend unavailable.", "TransportError"};
    }

    const auto result = client->setStrictMode(runtime_.sessionId(), enabled);
    if (!result.ok && result.errorCode == "SessionExpired") {
        std::string ignored;
        (void)recoverGuestSession(runtime_, ignored);
        return FacadeResult{false, defaultFailureMessage("Backend session expired.", result.message), result.errorCode};
    }

    return FacadeResult{
        result.ok,
        result.message,
        result.errorCode
    };
}

FacadeResult BackendFacade::createInitialAdmin(
    const std::string& username,
    const std::string& password,
    const std::string& passwordHint
) {
    std::string message;
    if (!ensureSession(message)) {
        return FacadeResult{false, message, "TransportError"};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return FacadeResult{false, "Backend unavailable.", "TransportError"};
    }

    const auto result = client->createInitialAdmin(runtime_.sessionId(), username, password, passwordHint);
    if (!result.ok && result.errorCode == "SessionExpired") {
        std::string ignored;
        (void)recoverGuestSession(runtime_, ignored);
        return FacadeResult{false, defaultFailureMessage("Backend session expired.", result.message), result.errorCode};
    }

    return FacadeResult{
        result.ok,
        result.message,
        result.errorCode
    };
}

ClientResult<std::vector<std::string>> BackendFacade::readTlog(const std::string& path) {
    std::string message;
    if (!ensureSession(message)) {
        return ClientResult<std::vector<std::string>>{false, {}, "TransportError", message};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return ClientResult<std::vector<std::string>>{false, {}, "TransportError", "Backend unavailable."};
    }

    const auto lines = client->readTlog(runtime_.sessionId(), path);
    if (!lines.ok && lines.errorCode == "SessionExpired") {
        std::string ignored;
        (void)recoverGuestSession(runtime_, ignored);
        return ClientResult<std::vector<std::string>>{
            false,
            {},
            lines.errorCode,
            defaultFailureMessage("Backend session expired.", lines.message)
        };
    }

    return lines;
}

ClientResult<std::string> BackendFacade::exportTlog(const std::string& path) {
    std::string message;
    if (!ensureSession(message)) {
        return ClientResult<std::string>{false, {}, "TransportError", message};
    }

    auto* client = runtime_.client();
    if (client == nullptr) {
        return ClientResult<std::string>{false, {}, "TransportError", "Backend unavailable."};
    }

    const auto content = client->exportTlog(runtime_.sessionId(), path);
    if (!content.ok && content.errorCode == "SessionExpired") {
        std::string ignored;
        (void)recoverGuestSession(runtime_, ignored);
        return ClientResult<std::string>{
            false,
            {},
            content.errorCode,
            defaultFailureMessage("Backend session expired.", content.message)
        };
    }

    return content;
}

} // namespace tundraux::frontend

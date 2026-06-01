#pragma once

#include "backend_error.hpp"
#include "user_store.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace tundraux::backend {

template <typename T>
struct ServiceResult {
    bool ok = false;
    T value{};
    BackendError error{ErrorCode::InternalError, "Internal error."};

    static ServiceResult success(T result) {
        ServiceResult out;
        out.ok = true;
        out.value = std::move(result);
        return out;
    }

    static ServiceResult failure(ErrorCode code, std::string message) {
        ServiceResult out;
        out.ok = false;
        out.error = BackendError{code, std::move(message)};
        return out;
    }
};

struct EmptyResult {};

struct SessionInfo {
    std::string sessionId;
    BackendUser user;
};

class SessionService {
public:
    explicit SessionService(UserStore& users);

    SessionInfo startGuestSession();
    SessionInfo startSession(BackendUser user);
    ServiceResult<SessionInfo> login(
        const std::string& sessionId,
        const std::string& username,
        const std::string& password
    );
    ServiceResult<EmptyResult> logout(const std::string& sessionId);
    ServiceResult<BackendUser> whoami(const std::string& sessionId) const;
    ServiceResult<BackendUser> requireSession(const std::string& sessionId) const;

private:
    UserStore& users_;
    std::unordered_map<std::string, BackendUser> sessions_;

    std::string nextSessionId();
    static BackendUser guestUser();
};

} // namespace tundraux::backend

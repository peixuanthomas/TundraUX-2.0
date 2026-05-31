#include "session_service.hpp"

#include <algorithm>
#include <utility>

namespace tundraux::backend {

SessionService::SessionService(UserStore& users) : users_(users) {}

BackendUser SessionService::guestUser() {
    return BackendUser{"guest", "", "", "", 0};
}

std::string SessionService::nextSessionId() {
    return "session-" + std::to_string(nextSessionId_++);
}

SessionInfo SessionService::startGuestSession() {
    SessionInfo session{nextSessionId(), guestUser()};
    sessions_[session.sessionId] = session.user;
    return session;
}

ServiceResult<SessionInfo> SessionService::login(
    const std::string& sessionId,
    const std::string& username,
    const std::string& password
) {
    auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::SessionExpired, "Session expired.");
    }

    auto users = users_.listUsers();
    auto found = std::find_if(users.begin(), users.end(), [&](const BackendUser& user) {
        return user.name == username;
    });
    if (found == users.end()) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::AuthenticationFailed, "User not found.");
    }
    if (found->failedCount > 7) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::PermissionDenied, "User disabled due to too many failed attempts.");
    }
    if (found->password != password) {
        BackendUser updated = *found;
        updated.failedCount += 1;
        if (!users_.updateUser(found->name, updated)) {
            return ServiceResult<SessionInfo>::failure(ErrorCode::StorageError, "Unable to update user data.");
        }
        return ServiceResult<SessionInfo>::failure(ErrorCode::AuthenticationFailed, "Incorrect password.");
    }

    BackendUser updated = *found;
    updated.failedCount = 0;
    if (!users_.updateUser(found->name, updated)) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::StorageError, "Unable to update user data.");
    }
    session->second = updated;
    return ServiceResult<SessionInfo>::success(SessionInfo{sessionId, updated});
}

ServiceResult<EmptyResult> SessionService::logout(const std::string& sessionId) {
    auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::SessionExpired, "Session expired.");
    }
    session->second = guestUser();
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<BackendUser> SessionService::whoami(const std::string& sessionId) const {
    auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::SessionExpired, "Session expired.");
    }
    return ServiceResult<BackendUser>::success(session->second);
}

} // namespace tundraux::backend

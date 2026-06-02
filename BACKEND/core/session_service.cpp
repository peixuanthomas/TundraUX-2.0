#include "session_service.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace tundraux::backend {
namespace {

std::string randomSessionId() {
    static thread_local std::mt19937_64 generator([] {
        std::random_device device;
        std::seed_seq seed{
            device(), device(), device(), device(),
            device(), device(), device(), device()
        };
        return std::mt19937_64(seed);
    }());

    std::uniform_int_distribution<std::uint64_t> distribution;
    std::ostringstream out;
    out << std::hex << std::setfill('0')
        << std::setw(16) << distribution(generator)
        << std::setw(16) << distribution(generator);
    return out.str();
}

} // namespace

SessionService::SessionService(UserStore& users) : users_(users) {}

BackendUser SessionService::guestUser() {
    return BackendUser{"guest", "", "", "", 0};
}

std::string SessionService::nextSessionId() {
    std::string sessionId;
    do {
        sessionId = randomSessionId();
    } while (sessions_.find(sessionId) != sessions_.end());
    return sessionId;
}

SessionInfo SessionService::startGuestSession() {
    return startSession(guestUser());
}

SessionInfo SessionService::startSession(BackendUser user) {
    SessionInfo session{nextSessionId(), std::move(user)};
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

    std::vector<BackendUser> users;
    try {
        users = users_.listUsers();
    } catch (const std::exception&) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::StorageError, "Unable to read user data.");
    }

    auto found = std::find_if(users.begin(), users.end(), [&](const BackendUser& user) {
        return user.name == username;
    });
    if (found == users.end()) {
        return ServiceResult<SessionInfo>::failure(
            ErrorCode::AuthenticationFailed,
            "User not found: " + username + "."
        );
    }
    if (found->failedCount > 7) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::PermissionDenied, "User disabled due to too many failed attempts.");
    }
    if (found->password != password) {
        BackendUser updated = *found;
        updated.failedCount += 1;
        try {
            if (!users_.updateUser(found->name, updated)) {
                return ServiceResult<SessionInfo>::failure(ErrorCode::StorageError, "Unable to update user data.");
            }
        } catch (const std::exception&) {
            return ServiceResult<SessionInfo>::failure(ErrorCode::StorageError, "Unable to update user data.");
        }
        return ServiceResult<SessionInfo>::failure(
            ErrorCode::AuthenticationFailed,
            "Incorrect password for user " + username + "."
        );
    }

    BackendUser updated = *found;
    updated.failedCount = 0;
    try {
        if (!users_.updateUser(found->name, updated)) {
            return ServiceResult<SessionInfo>::failure(ErrorCode::StorageError, "Unable to update user data.");
        }
    } catch (const std::exception&) {
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

ServiceResult<BackendUser> SessionService::requireSession(const std::string& sessionId) const {
    return whoami(sessionId);
}

} // namespace tundraux::backend

#pragma once

#include "session_service.hpp"
#include "user_store.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class UserService {
public:
    UserService(UserStore& users, const SessionService& sessions);

    ServiceResult<std::vector<BackendUser>> listUsers(const std::string& sessionId) const;
    ServiceResult<BackendUser> currentProfile(const std::string& sessionId) const;
    ServiceResult<EmptyResult> createInitialAdmin(
        const std::string& sessionId,
        const std::string& username,
        const std::string& password,
        const std::string& passwordHint
    );
    ServiceResult<EmptyResult> createUser(const std::string& sessionId, const BackendUser& user);
    ServiceResult<EmptyResult> updateUser(
        const std::string& sessionId,
        const std::string& originalName,
        const BackendUser& user,
        bool passwordProvided
    );
    ServiceResult<EmptyResult> deleteUser(const std::string& sessionId, const std::string& name);
    ServiceResult<EmptyResult> resetFailedCount(const std::string& sessionId, const std::string& name);
    ServiceResult<EmptyResult> disableUser(const std::string& sessionId, const std::string& name);
    ServiceResult<EmptyResult> updateOwnAccount(
        const std::string& sessionId,
        bool passwordProvided,
        const std::string& password,
        bool passwordHintProvided,
        const std::string& passwordHint
    );
    ServiceResult<bool> getStrictMode(const std::string& sessionId) const;
    ServiceResult<EmptyResult> setStrictMode(const std::string& sessionId, bool enabled);

private:
    UserStore& users_;
    const SessionService& sessions_;

    static bool canManageUsers(const BackendUser& user);
    ServiceResult<BackendUser> requireStoredSessionUser(const std::string& sessionId) const;
    ServiceResult<BackendUser> requireUserManager(const std::string& sessionId) const;
    ServiceResult<std::vector<BackendUser>> loadUsers() const;
};

} // namespace tundraux::backend

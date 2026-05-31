#include "user_service.hpp"

#include <exception>

namespace tundraux::backend {

UserService::UserService(UserStore& users, const SessionService& sessions)
    : users_(users), sessions_(sessions) {}

bool UserService::canManageUsers(const BackendUser& user) {
    return user.type == "admin" || user.type == "debug";
}

ServiceResult<std::vector<BackendUser>> UserService::listUsers(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<std::vector<BackendUser>>::failure(session.error.code, session.error.message);
    }
    if (!canManageUsers(session.value)) {
        return ServiceResult<std::vector<BackendUser>>::failure(ErrorCode::PermissionDenied, "Access Denied.");
    }

    std::vector<BackendUser> users;
    try {
        users = users_.listUsers();
    } catch (const std::exception&) {
        return ServiceResult<std::vector<BackendUser>>::failure(ErrorCode::StorageError, "Unable to read user data.");
    }

    for (auto& user : users) {
        user.password.clear();
    }
    return ServiceResult<std::vector<BackendUser>>::success(users);
}

} // namespace tundraux::backend

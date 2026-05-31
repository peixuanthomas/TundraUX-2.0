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

private:
    UserStore& users_;
    const SessionService& sessions_;

    static bool canManageUsers(const BackendUser& user);
};

} // namespace tundraux::backend

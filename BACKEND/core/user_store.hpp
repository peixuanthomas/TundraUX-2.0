#pragma once

#include <string>
#include <vector>

namespace tundraux::backend {

struct BackendUser {
    std::string type;
    std::string name;
    std::string password;
    std::string passwordHint;
    int failedCount = 0;
};

class UserStore {
public:
    virtual ~UserStore() = default;
    virtual std::vector<BackendUser> listUsers() const = 0;
    virtual bool updateUser(const std::string& name, const BackendUser& user) = 0;
};

} // namespace tundraux::backend

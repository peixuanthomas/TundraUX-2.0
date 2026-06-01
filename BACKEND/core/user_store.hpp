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
    virtual bool addUser(const BackendUser& user) = 0;
    virtual bool updateUser(const std::string& name, const BackendUser& user) = 0;
    virtual bool removeUser(const std::string& name) = 0;
    virtual bool getStrictMode() const = 0;
    virtual bool setStrictMode(bool enabled) = 0;
};

} // namespace tundraux::backend

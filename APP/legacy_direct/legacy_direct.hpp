#pragma once

#include "backend_facade.hpp"

#include <string>
#include <vector>

namespace tundraux::legacy_direct {

using ShellUser = tundraux::frontend::ShellUser;

struct LoginResult {
    bool ok = false;
    ShellUser user;
    std::string message;
    std::string passwordHint;
    int failedCount = 0;
};

struct AccountRecord {
    ShellUser user;
    std::string password;
};

bool login(const std::string& username, const std::string& password, LoginResult& result);
bool listUsers(std::vector<ShellUser>& users, std::string& message);
bool getStrictMode(bool& enabled, std::string& message);
bool setStrictMode(bool enabled, std::string& message);
bool loadAccount(const ShellUser& currentUser, AccountRecord& record, std::string& message);
bool saveAccount(
    const std::string& originalName,
    bool passwordProvided,
    const std::string& password,
    const std::string& passwordHint,
    AccountRecord& record,
    std::string& message
);
bool debugCreateFile(std::string& message);
bool debugDeleteFile(std::string& message);
bool debugForceLogin(const std::string& username, ShellUser& currentUser, std::string& message);
bool createInitialAdmin(
    const std::string& username,
    const std::string& password,
    const std::string& passwordHint,
    std::string& message
);

} // namespace tundraux::legacy_direct

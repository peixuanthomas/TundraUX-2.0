#include "legacy_direct.hpp"

#include "udata.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace tundraux::audit {
void setStrictModeEnabled(bool enabled);
}

namespace tundraux::legacy_direct {
namespace {

constexpr const char* kUserDataPath = "user_data.dat";

ShellUser toShellUser(const USER& user) {
    return {
        user.type,
        user.name,
        user.password_hint,
        user.count
    };
}

USER toLegacyUser(const ShellUser& user, const std::string& password = "") {
    return {
        user.type,
        user.name,
        password,
        user.passwordHint,
        user.failedCount
    };
}

const USER* findUser(const std::vector<USER>& users, const std::string& username) {
    const auto it = std::find_if(users.begin(), users.end(), [&](const USER& user) {
        return user.name == username;
    });
    return it == users.end() ? nullptr : &(*it);
}

std::string trimCopy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool hasWhitespace(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(static_cast<unsigned char>(ch)) != 0;
    });
}

bool passwordMeetsRules(const std::string& password) {
    bool hasUpper = false;
    bool hasLower = false;
    bool hasDigit = false;
    for (const char ch : password) {
        const auto value = static_cast<unsigned char>(ch);
        hasUpper = hasUpper || std::isupper(value) != 0;
        hasLower = hasLower || std::islower(value) != 0;
        hasDigit = hasDigit || std::isdigit(value) != 0;
    }
    return password.size() >= 6 && hasUpper && hasLower && hasDigit;
}

bool validateInitialAdminInput(
    const std::string& username,
    const std::string& password,
    const std::string& passwordHint,
    std::string& normalizedUsername,
    std::string& normalizedPasswordHint,
    std::string& message
) {
    normalizedUsername = trimCopy(username);
    normalizedPasswordHint = trimCopy(passwordHint);
    if (normalizedUsername.empty()) {
        message = "Username cannot be empty.";
        return false;
    }
    if (normalizedUsername == "null") {
        message = "\"null\" is reserved for setup.";
        return false;
    }
    if (hasWhitespace(normalizedUsername)) {
        message = "Username cannot contain spaces.";
        return false;
    }
    if (!passwordMeetsRules(password)) {
        message = "Password must be 6+ chars with uppercase, lowercase, and number.";
        return false;
    }
    if (!normalizedPasswordHint.empty() && normalizedPasswordHint == password) {
        message = "Password hint cannot equal the password.";
        return false;
    }
    return true;
}

bool ensureStoreExists(std::string& message) {
    std::ifstream file(kUserDataPath);
    if (!file.good()) {
        message = "Error: user_data.dat not found.";
        return false;
    }
    return true;
}

void syncAuditStrictMode(const DataManager& dataManager) {
    tundraux::audit::setStrictModeEnabled(dataManager.GetStrictMode());
}

} // namespace

bool login(const std::string& username, const std::string& password, LoginResult& result) {
    result = {};
    DataManager dataManager(kUserDataPath);
    syncAuditStrictMode(dataManager);
    const auto& users = dataManager.GetAllUsers();
    const USER* storedUser = findUser(users, username);
    if (storedUser == nullptr) {
        result.message = "User not found: " + username;
        return false;
    }

    if (storedUser->count > 7) {
        result.message = "User disabled due to too many failed attempts.";
        return false;
    }

    if (dataManager.ComparePassword(username, password)) {
        USER updated = *storedUser;
        updated.count = 0;
        dataManager.UpdateUser(username, updated);
        result.ok = true;
        result.user = toShellUser(updated);
        return true;
    }

    USER updated = *storedUser;
    updated.count += 1;
    dataManager.UpdateUser(username, updated);
    result.message = "Incorrect password for user " + username + ".";
    result.passwordHint = storedUser->password_hint;
    result.failedCount = updated.count;
    return false;
}

bool listUsers(std::vector<ShellUser>& users, std::string& message) {
    users.clear();
    if (!ensureStoreExists(message)) {
        return false;
    }

    DataManager dataManager(kUserDataPath);
    for (const auto& user : dataManager.GetAllUsers()) {
        users.push_back(toShellUser(user));
    }
    return true;
}

bool getStrictMode(bool& enabled, std::string& message) {
    if (!ensureStoreExists(message)) {
        return false;
    }

    DataManager dataManager(kUserDataPath);
    enabled = dataManager.GetStrictMode();
    tundraux::audit::setStrictModeEnabled(enabled);
    return true;
}

bool setStrictMode(bool enabled, std::string& message) {
    if (!ensureStoreExists(message)) {
        return false;
    }

    DataManager dataManager(kUserDataPath);
    if (!dataManager.SetStrictMode(enabled)) {
        message = "Failed to update strict mode.";
        return false;
    }
    tundraux::audit::setStrictModeEnabled(enabled);
    return true;
}

bool loadAccount(const ShellUser& currentUser, AccountRecord& record, std::string& message) {
    if (!ensureStoreExists(message)) {
        return false;
    }

    DataManager dataManager(kUserDataPath);
    const USER* storedUser = findUser(dataManager.GetAllUsers(), currentUser.name);
    if (storedUser == nullptr) {
        message = "Current user is not stored in user_data.dat.";
        return false;
    }

    record.user = toShellUser(*storedUser);
    record.password = storedUser->password;
    return true;
}

bool saveAccount(
    const std::string& originalName,
    bool passwordProvided,
    const std::string& password,
    const std::string& passwordHint,
    AccountRecord& record,
    std::string& message
) {
    USER updated = toLegacyUser(record.user, record.password);
    if (passwordProvided) {
        updated.password = password;
    }
    updated.password_hint = passwordHint;

    DataManager dataManager(kUserDataPath);
    if (!dataManager.UpdateUser(originalName, updated)) {
        message = "Failed to update user info.";
        return false;
    }

    record.user = toShellUser(updated);
    record.password = updated.password;
    return true;
}

bool debugCreateFile(std::string& message) {
    createfile();

    USER debugUser;
    debugUser.type = "admin";
    debugUser.name = "Admin";
    debugUser.password = "";
    debugUser.password_hint = "Default admin user created by dbg:createfile command.";
    debugUser.count = 0;

    DataManager dataManager(kUserDataPath);
    dataManager.AddUser(debugUser);
    debugUser.type = "user";
    debugUser.name = "User";
    debugUser.password_hint = "Default regular user created by dbg:createfile command.";
    dataManager.AddUser(debugUser);
    dataManager.RemoveUser("null");

    message = "Legacy user data file created.";
    return true;
}

bool debugDeleteFile(std::string& message) {
    if (std::remove(kUserDataPath) == 0) {
        message = "User data file deleted successfully.";
        return true;
    }
    message = "Error deleting user data file or file does not exist.";
    return false;
}

bool debugForceLogin(const std::string& username, ShellUser& currentUser, std::string& message) {
    DataManager dataManager(kUserDataPath);
    const USER* storedUser = findUser(dataManager.GetAllUsers(), username);
    if (storedUser == nullptr) {
        message = "[DBG] User not found: " + username;
        return false;
    }

    currentUser = toShellUser(*storedUser);
    return true;
}

bool createInitialAdmin(
    const std::string& username,
    const std::string& password,
    const std::string& passwordHint,
    std::string& message
) {
    std::error_code error;
    const bool exists = std::filesystem::exists(kUserDataPath, error);
    if (error) {
        message = "Unable to read user data.";
        return false;
    }

    if (exists && !std::filesystem::is_regular_file(kUserDataPath, error)) {
        message = "Unable to read user data.";
        return false;
    }
    if (error) {
        message = "Unable to read user data.";
        return false;
    }

    bool emptyFile = false;
    if (exists) {
        const auto size = std::filesystem::file_size(kUserDataPath, error);
        if (error) {
            message = "Unable to read user data.";
            return false;
        }
        emptyFile = size == 0;
    }

    std::string normalizedUsername;
    std::string normalizedPasswordHint;
    if (!exists || emptyFile) {
        if (!validateInitialAdminInput(
                username,
                password,
                passwordHint,
                normalizedUsername,
                normalizedPasswordHint,
                message)) {
            return false;
        }
        if (emptyFile) {
            std::filesystem::remove(kUserDataPath, error);
            if (error) {
                message = "Unable to update user data.";
                return false;
            }
        }
        createfile();
        if (!std::filesystem::exists(kUserDataPath, error) || error) {
            message = "Unable to create initial admin.";
            return false;
        }
    }

    DataManager dataManager(kUserDataPath);
    const auto& users = dataManager.GetAllUsers();
    if (exists && !emptyFile && users.empty()) {
        message = "Unable to read user data.";
        return false;
    }
    const bool onlyPlaceholder = users.size() == 1 && users.front().name == "null";
    if (!users.empty() && !onlyPlaceholder) {
        message = "Setup already initialized.";
        return false;
    }

    if (exists && !emptyFile) {
        if (!validateInitialAdminInput(
                username,
                password,
                passwordHint,
                normalizedUsername,
                normalizedPasswordHint,
                message)) {
            return false;
        }
    }

    USER admin;
    admin.type = "admin";
    admin.name = normalizedUsername;
    admin.password = password;
    admin.password_hint = normalizedPasswordHint;
    admin.count = 0;

    if (!dataManager.AddUser(admin)) {
        message = "Unable to create initial admin.";
        return false;
    }
    if (onlyPlaceholder && !dataManager.RemoveUser("null")) {
        (void)dataManager.RemoveUser(admin.name);
        message = "Unable to create initial admin.";
        return false;
    }

    message = "Admin user created.";
    return true;
}

} // namespace tundraux::legacy_direct

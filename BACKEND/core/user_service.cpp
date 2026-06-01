#include "user_service.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <utility>

namespace tundraux::backend {
namespace {

constexpr const char* kAccessDeniedMessage = "Access Denied.";
constexpr const char* kReadUserDataError = "Unable to read user data.";
constexpr const char* kUpdateUserDataError = "Unable to update user data.";

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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
        return std::isspace(ch) != 0;
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

bool isActiveAdmin(const BackendUser& user) {
    return lowerAscii(user.type) == "admin" && user.failedCount <= 7;
}

std::size_t activeAdminCount(const std::vector<BackendUser>& users, const std::string& excludingName = "") {
    std::size_t count = 0;
    for (const auto& user : users) {
        if (user.name == excludingName) {
            continue;
        }
        if (isActiveAdmin(user)) {
            ++count;
        }
    }
    return count;
}

bool nameExists(const std::vector<BackendUser>& users, const std::string& name, const std::string& exceptName = "") {
    return std::any_of(users.begin(), users.end(), [&](const BackendUser& user) {
        return user.name == name && user.name != exceptName;
    });
}

BackendUser withoutPassword(BackendUser user) {
    user.password.clear();
    return user;
}

ServiceResult<EmptyResult> validationFailure(std::string message) {
    return ServiceResult<EmptyResult>::failure(ErrorCode::InvalidParams, std::move(message));
}

std::string validateManagedUser(
    const BackendUser& user,
    const std::vector<BackendUser>& existingUsers,
    const std::string& originalName,
    bool editing,
    bool passwordProvided
) {
    if (user.name.empty()) {
        return "Username cannot be empty.";
    }
    if (user.name == "null") {
        return "\"null\" is reserved for setup.";
    }
    if (hasWhitespace(user.name)) {
        return "Username cannot contain spaces.";
    }
    if (nameExists(existingUsers, user.name, editing ? originalName : "")) {
        return "Username already exists.";
    }
    const std::string type = lowerAscii(user.type);
    if (type == "debug") {
        return "Debug users cannot be created or edited here.";
    }
    if (type != "admin" && type != "user") {
        return "Type must be admin or user.";
    }
    if (passwordProvided && user.password.empty()) {
        return "Password cannot be empty.";
    }
    if (passwordProvided && !passwordMeetsRules(user.password)) {
        return "Password must be 6+ chars with uppercase, lowercase, and number.";
    }
    if (!user.passwordHint.empty() && user.passwordHint == user.password) {
        return "Password hint cannot equal the password.";
    }
    if (user.failedCount < 0 || user.failedCount > 8) {
        return "Failed count must be between 0 and 8.";
    }

    if (editing) {
        const auto oldUser = std::find_if(existingUsers.begin(), existingUsers.end(), [&](const BackendUser& value) {
            return value.name == originalName;
        });
        if (oldUser == existingUsers.end()) {
            return "User not found.";
        }
        if (isActiveAdmin(*oldUser) && (type != "admin" || user.failedCount > 7) &&
            activeAdminCount(existingUsers, oldUser->name) == 0) {
            return "At least one active admin user is required.";
        }
    }

    return "";
}

} // namespace

UserService::UserService(UserStore& users, const SessionService& sessions)
    : users_(users), sessions_(sessions) {}

bool UserService::canManageUsers(const BackendUser& user) {
    const std::string type = lowerAscii(user.type);
    return type == "admin" || type == "debug";
}

ServiceResult<BackendUser> UserService::requireStoredSessionUser(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<BackendUser>::failure(session.error.code, session.error.message);
    }
    if (session.value.type == "guest" || session.value.name.empty()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }

    const auto users = loadUsers();
    if (!users.ok) {
        return ServiceResult<BackendUser>::failure(users.error.code, users.error.message);
    }

    const auto found = std::find_if(users.value.begin(), users.value.end(), [&](const BackendUser& user) {
        return user.name == session.value.name;
    });
    if (found == users.value.end()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::NotFound, "User not found.");
    }
    if (found->failedCount > 7) {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }

    return ServiceResult<BackendUser>::success(*found);
}

ServiceResult<BackendUser> UserService::requireUserManager(const std::string& sessionId) const {
    const auto user = requireStoredSessionUser(sessionId);
    if (!user.ok) {
        return ServiceResult<BackendUser>::failure(user.error.code, user.error.message);
    }
    if (!canManageUsers(user.value)) {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    return user;
}

ServiceResult<std::vector<BackendUser>> UserService::loadUsers() const {
    try {
        return ServiceResult<std::vector<BackendUser>>::success(users_.listUsers());
    } catch (const std::exception&) {
        return ServiceResult<std::vector<BackendUser>>::failure(ErrorCode::StorageError, kReadUserDataError);
    }
}

ServiceResult<std::vector<BackendUser>> UserService::listUsers(const std::string& sessionId) const {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<std::vector<BackendUser>>::failure(manager.error.code, manager.error.message);
    }

    auto users = loadUsers();
    if (!users.ok) {
        return users;
    }

    for (auto& user : users.value) {
        user.password.clear();
    }
    return users;
}

ServiceResult<BackendUser> UserService::currentProfile(const std::string& sessionId) const {
    const auto currentUser = requireStoredSessionUser(sessionId);
    if (!currentUser.ok) {
        return ServiceResult<BackendUser>::failure(currentUser.error.code, currentUser.error.message);
    }
    return ServiceResult<BackendUser>::success(withoutPassword(currentUser.value));
}

ServiceResult<EmptyResult> UserService::createUser(const std::string& sessionId, const BackendUser& user) {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<EmptyResult>::failure(manager.error.code, manager.error.message);
    }
    auto users = loadUsers();
    if (!users.ok) {
        return ServiceResult<EmptyResult>::failure(users.error.code, users.error.message);
    }

    BackendUser normalized = user;
    normalized.name = trimCopy(normalized.name);
    normalized.type = lowerAscii(trimCopy(normalized.type));
    normalized.passwordHint = trimCopy(normalized.passwordHint);
    const std::string validationError = validateManagedUser(normalized, users.value, "", false, true);
    if (!validationError.empty()) {
        return validationFailure(validationError);
    }

    try {
        if (!users_.addUser(normalized)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::Conflict, "Unable to create user.");
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
    }
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<EmptyResult> UserService::updateUser(
    const std::string& sessionId,
    const std::string& originalName,
    const BackendUser& user,
    bool passwordProvided
) {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<EmptyResult>::failure(manager.error.code, manager.error.message);
    }
    auto users = loadUsers();
    if (!users.ok) {
        return ServiceResult<EmptyResult>::failure(users.error.code, users.error.message);
    }

    const auto existing = std::find_if(users.value.begin(), users.value.end(), [&](const BackendUser& value) {
        return value.name == originalName;
    });
    if (existing == users.value.end()) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::NotFound, "User not found.");
    }

    BackendUser normalized = user;
    normalized.name = trimCopy(normalized.name);
    normalized.type = lowerAscii(trimCopy(normalized.type));
    normalized.passwordHint = trimCopy(normalized.passwordHint);
    if (!passwordProvided) {
        normalized.password = existing->password;
    }

    const std::string validationError = validateManagedUser(normalized, users.value, originalName, true, passwordProvided);
    if (!validationError.empty()) {
        return validationFailure(validationError);
    }

    try {
        if (!users_.updateUser(originalName, normalized)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::Conflict, "Unable to update user.");
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
    }
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<EmptyResult> UserService::deleteUser(const std::string& sessionId, const std::string& name) {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<EmptyResult>::failure(manager.error.code, manager.error.message);
    }
    auto users = loadUsers();
    if (!users.ok) {
        return ServiceResult<EmptyResult>::failure(users.error.code, users.error.message);
    }

    const auto existing = std::find_if(users.value.begin(), users.value.end(), [&](const BackendUser& user) {
        return user.name == name;
    });
    if (existing == users.value.end()) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::NotFound, "User not found.");
    }
    if (isActiveAdmin(*existing) && activeAdminCount(users.value, existing->name) == 0) {
        return validationFailure("At least one active admin user is required.");
    }

    try {
        if (!users_.removeUser(name)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::Conflict, "Unable to delete user.");
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
    }
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<EmptyResult> UserService::resetFailedCount(const std::string& sessionId, const std::string& name) {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<EmptyResult>::failure(manager.error.code, manager.error.message);
    }
    auto users = loadUsers();
    if (!users.ok) {
        return ServiceResult<EmptyResult>::failure(users.error.code, users.error.message);
    }
    auto existing = std::find_if(users.value.begin(), users.value.end(), [&](const BackendUser& user) {
        return user.name == name;
    });
    if (existing == users.value.end()) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::NotFound, "User not found.");
    }
    existing->failedCount = 0;
    try {
        if (!users_.updateUser(name, *existing)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::Conflict, "Unable to reset failed count.");
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
    }
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<EmptyResult> UserService::disableUser(const std::string& sessionId, const std::string& name) {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<EmptyResult>::failure(manager.error.code, manager.error.message);
    }
    auto users = loadUsers();
    if (!users.ok) {
        return ServiceResult<EmptyResult>::failure(users.error.code, users.error.message);
    }
    auto existing = std::find_if(users.value.begin(), users.value.end(), [&](const BackendUser& user) {
        return user.name == name;
    });
    if (existing == users.value.end()) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::NotFound, "User not found.");
    }
    if (isActiveAdmin(*existing) && activeAdminCount(users.value, existing->name) == 0) {
        return validationFailure("At least one active admin user is required.");
    }
    existing->failedCount = 8;
    try {
        if (!users_.updateUser(name, *existing)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::Conflict, "Unable to disable user.");
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
    }
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<EmptyResult> UserService::updateOwnAccount(
    const std::string& sessionId,
    bool passwordProvided,
    const std::string& password,
    bool passwordHintProvided,
    const std::string& passwordHint
) {
    const auto currentUser = requireStoredSessionUser(sessionId);
    if (!currentUser.ok) {
        return ServiceResult<EmptyResult>::failure(currentUser.error.code, currentUser.error.message);
    }
    if (lowerAscii(currentUser.value.type) == "debug") {
        return ServiceResult<EmptyResult>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    BackendUser updated = currentUser.value;
    if (passwordProvided) {
        if (!passwordMeetsRules(password)) {
            return validationFailure("Password must be 6+ chars with uppercase, lowercase, and number.");
        }
        updated.password = password;
    }
    if (passwordHintProvided) {
        updated.passwordHint = trimCopy(passwordHint);
    }
    if (!updated.passwordHint.empty() && updated.passwordHint == updated.password) {
        return validationFailure("Password hint cannot equal the password.");
    }
    try {
        if (!users_.updateUser(currentUser.value.name, updated)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::Conflict, "Unable to update account.");
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
    }
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<bool> UserService::getStrictMode(const std::string& sessionId) const {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<bool>::failure(manager.error.code, manager.error.message);
    }
    try {
        return ServiceResult<bool>::success(users_.getStrictMode());
    } catch (const std::exception&) {
        return ServiceResult<bool>::failure(ErrorCode::StorageError, kReadUserDataError);
    }
}

ServiceResult<EmptyResult> UserService::setStrictMode(const std::string& sessionId, bool enabled) {
    const auto manager = requireUserManager(sessionId);
    if (!manager.ok) {
        return ServiceResult<EmptyResult>::failure(manager.error.code, manager.error.message);
    }
    try {
        if (!users_.setStrictMode(enabled)) {
            return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
        }
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kUpdateUserDataError);
    }
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

} // namespace tundraux::backend

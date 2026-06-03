#include "legacy_direct.hpp"
#include "udata.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace tundraux::audit {
void setStrictModeEnabled(bool) {}
}

namespace {

class ScopedWorkspace {
public:
    explicit ScopedWorkspace(const std::string& name)
        : original_(std::filesystem::current_path()),
          path_(original_ / name) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
        std::filesystem::current_path(path_, error);
        if (error) {
            std::cerr << "failed to enter workspace: " << error.message() << "\n";
            valid_ = false;
        }
    }

    ~ScopedWorkspace() {
        std::error_code error;
        std::filesystem::current_path(original_, error);
        std::filesystem::remove_all(path_, error);
    }

    bool valid() const {
        return valid_;
    }

private:
    std::filesystem::path original_;
    std::filesystem::path path_;
    bool valid_ = true;
};

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool storedAdminMatches(const std::string& label) {
    DataManager dataManager("user_data.dat");
    const auto& users = dataManager.GetAllUsers();
    return expect(users.size() == 1, label + " should create exactly one user") &&
        expect(users.front().type == "admin", label + " user type mismatch") &&
        expect(users.front().name == "setup-admin", label + " username mismatch") &&
        expect(users.front().password == "Secret1", label + " password mismatch") &&
        expect(users.front().password_hint == "primary", label + " password hint mismatch") &&
        expect(users.front().count == 0, label + " failed count mismatch");
}

bool createsInitialAdminFromMissingStore() {
    ScopedWorkspace workspace("legacy_direct_missing_store_test");
    if (!workspace.valid()) {
        return false;
    }

    std::string message;
    const bool created = tundraux::legacy_direct::createInitialAdmin(
        "setup-admin",
        "Secret1",
        "primary",
        message
    );

    return expect(created, "missing store initial admin should be created: " + message) &&
        expect(message == "Admin user created.", "missing store message mismatch: " + message) &&
        storedAdminMatches("missing store");
}

bool createsInitialAdminFromEmptyStore() {
    ScopedWorkspace workspace("legacy_direct_empty_store_test");
    if (!workspace.valid()) {
        return false;
    }
    {
        std::ofstream file("user_data.dat", std::ios::binary | std::ios::trunc);
    }

    std::string message;
    const bool created = tundraux::legacy_direct::createInitialAdmin(
        "setup-admin",
        "Secret1",
        "primary",
        message
    );

    return expect(created, "empty store initial admin should be created: " + message) &&
        expect(message == "Admin user created.", "empty store message mismatch: " + message) &&
        storedAdminMatches("empty store");
}

bool rejectsInvalidInitialAdminPasswords() {
    ScopedWorkspace workspace("legacy_direct_validation_test");
    if (!workspace.valid()) {
        return false;
    }

    std::string message;
    bool created = tundraux::legacy_direct::createInitialAdmin(
        "setup-admin",
        "secret",
        "primary",
        message
    );
    if (!expect(!created, "weak password should be rejected")) {
        return false;
    }
    if (!expect(
            message == "Password must be 6+ chars with uppercase, lowercase, and number.",
            "weak password message mismatch: " + message)) {
        return false;
    }
    if (!expect(!std::filesystem::exists("user_data.dat"), "weak password should not create store")) {
        return false;
    }

    created = tundraux::legacy_direct::createInitialAdmin(
        "setup-admin",
        "Secret1",
        "Secret1",
        message
    );
    return expect(!created, "password hint equal to password should be rejected") &&
        expect(message == "Password hint cannot equal the password.", "hint message mismatch: " + message) &&
        expect(!std::filesystem::exists("user_data.dat"), "invalid hint should not create store");
}

bool failedInitialAdminWriteLeavesSetupRetryable() {
    ScopedWorkspace workspace("legacy_direct_retryable_setup_test");
    if (!workspace.valid()) {
        return false;
    }

    createfile();
    const std::string tooLongPassword = "Secret1" + std::string(1024 * 1024, 'A');

    std::string message;
    const bool created = tundraux::legacy_direct::createInitialAdmin(
        "setup-admin",
        tooLongPassword,
        "primary",
        message
    );
    if (!expect(!created, "oversized initial admin write should fail")) {
        return false;
    }
    if (!expect(message == "Unable to create initial admin.", "oversized write message mismatch: " + message)) {
        return false;
    }

    DataManager failedWriteStore("user_data.dat");
    const auto& failedWriteUsers = failedWriteStore.GetAllUsers();
    if (!expect(failedWriteUsers.size() == 1, "failed write should leave setup placeholder")) {
        return false;
    }
    if (!expect(failedWriteUsers.front().name == "null", "failed write placeholder name mismatch")) {
        return false;
    }

    const bool retried = tundraux::legacy_direct::createInitialAdmin(
        "setup-admin",
        "Secret1",
        "primary",
        message
    );
    return expect(retried, "retry after failed write should create admin: " + message) &&
        expect(message == "Admin user created.", "retry message mismatch: " + message) &&
        storedAdminMatches("retry after failed write");
}

} // namespace

int main() {
    return createsInitialAdminFromMissingStore() &&
        createsInitialAdminFromEmptyStore() &&
        rejectsInvalidInitialAdminPasswords() &&
        failedInitialAdminWriteLeavesSetupRetryable()
        ? 0
        : 1;
}

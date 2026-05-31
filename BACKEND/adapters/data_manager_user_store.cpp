#include "data_manager_user_store.hpp"

#include "udata.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tundraux::backend {

namespace {

class ScopedStdoutSilencer {
public:
    ScopedStdoutSilencer() : previous_(std::cout.rdbuf(sink_.rdbuf())) {}
    ~ScopedStdoutSilencer() {
        std::cout.rdbuf(previous_);
    }

private:
    std::ostringstream sink_;
    std::streambuf* previous_;
};

bool isExistingNonEmptyFile(const std::string& filename) {
    std::error_code error;
    if (!std::filesystem::exists(filename, error) || error) {
        return false;
    }
    const auto size = std::filesystem::file_size(filename, error);
    return !error && size > 0;
}

BackendUser toBackendUser(const USER& user) {
    BackendUser backendUser;
    backendUser.type = user.type;
    backendUser.name = user.name;
    backendUser.password = user.password;
    backendUser.passwordHint = user.password_hint;
    backendUser.failedCount = user.count;
    return backendUser;
}

USER toLegacyUser(const BackendUser& user) {
    USER legacyUser;
    legacyUser.type = user.type;
    legacyUser.name = user.name;
    legacyUser.password = user.password;
    legacyUser.password_hint = user.passwordHint;
    legacyUser.count = user.failedCount;
    return legacyUser;
}

} // namespace

DataManagerUserStore::DataManagerUserStore(std::string filename)
    : filename_(std::move(filename)) {}

std::vector<BackendUser> DataManagerUserStore::listUsers() const {
    const bool shouldHaveUsers = isExistingNonEmptyFile(filename_);
    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    std::vector<BackendUser> out;
    for (const auto& user : dataManager.GetAllUsers()) {
        out.push_back(toBackendUser(user));
    }
    if (shouldHaveUsers && out.empty()) {
        throw std::runtime_error("Unable to read user data.");
    }
    return out;
}

bool DataManagerUserStore::updateUser(const std::string& name, const BackendUser& user) {
    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    return dataManager.UpdateUser(name, toLegacyUser(user));
}

} // namespace tundraux::backend

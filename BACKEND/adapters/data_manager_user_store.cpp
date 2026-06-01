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

enum class StorageState {
    Missing,
    Invalid,
    ExistingEmptyFile,
    ExistingNonEmptyFile
};

StorageState classifyStorage(const std::string& filename) {
    std::error_code error;
    const bool exists = std::filesystem::exists(filename, error);
    if (error) {
        return StorageState::Invalid;
    }
    if (!exists) {
        return StorageState::Missing;
    }

    const bool regularFile = std::filesystem::is_regular_file(filename, error);
    if (error || !regularFile) {
        return StorageState::Invalid;
    }

    const auto size = std::filesystem::file_size(filename, error);
    if (error) {
        return StorageState::Invalid;
    }
    return size == 0 ? StorageState::ExistingEmptyFile : StorageState::ExistingNonEmptyFile;
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
    const StorageState storage = classifyStorage(filename_);
    if (storage == StorageState::Missing ||
        storage == StorageState::Invalid ||
        storage == StorageState::ExistingEmptyFile) {
        throw std::runtime_error("Unable to read user data.");
    }

    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    std::vector<BackendUser> out;
    for (const auto& user : dataManager.GetAllUsers()) {
        out.push_back(toBackendUser(user));
    }
    if (storage == StorageState::ExistingNonEmptyFile && out.empty()) {
        throw std::runtime_error("Unable to read user data.");
    }
    return out;
}

bool DataManagerUserStore::updateUser(const std::string& name, const BackendUser& user) {
    const StorageState storage = classifyStorage(filename_);
    if (storage == StorageState::Missing ||
        storage == StorageState::Invalid ||
        storage == StorageState::ExistingEmptyFile) {
        throw std::runtime_error("Unable to read user data.");
    }

    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    if (storage == StorageState::ExistingNonEmptyFile && dataManager.GetAllUsers().empty()) {
        throw std::runtime_error("Unable to read user data.");
    }
    return dataManager.UpdateUser(name, toLegacyUser(user));
}

bool DataManagerUserStore::addUser(const BackendUser& user) {
    const StorageState storage = classifyStorage(filename_);
    if (storage == StorageState::Missing ||
        storage == StorageState::Invalid ||
        storage == StorageState::ExistingEmptyFile) {
        throw std::runtime_error("Unable to read user data.");
    }

    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    if (storage == StorageState::ExistingNonEmptyFile && dataManager.GetAllUsers().empty()) {
        throw std::runtime_error("Unable to read user data.");
    }
    return dataManager.AddUser(toLegacyUser(user));
}

bool DataManagerUserStore::removeUser(const std::string& name) {
    const StorageState storage = classifyStorage(filename_);
    if (storage == StorageState::Missing ||
        storage == StorageState::Invalid ||
        storage == StorageState::ExistingEmptyFile) {
        throw std::runtime_error("Unable to read user data.");
    }

    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    if (storage == StorageState::ExistingNonEmptyFile && dataManager.GetAllUsers().empty()) {
        throw std::runtime_error("Unable to read user data.");
    }
    return dataManager.RemoveUser(name);
}

bool DataManagerUserStore::getStrictMode() const {
    const StorageState storage = classifyStorage(filename_);
    if (storage == StorageState::Missing ||
        storage == StorageState::Invalid ||
        storage == StorageState::ExistingEmptyFile) {
        throw std::runtime_error("Unable to read user data.");
    }

    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    if (storage == StorageState::ExistingNonEmptyFile && dataManager.GetAllUsers().empty()) {
        throw std::runtime_error("Unable to read user data.");
    }
    return dataManager.GetStrictMode();
}

bool DataManagerUserStore::setStrictMode(bool enabled) {
    const StorageState storage = classifyStorage(filename_);
    if (storage == StorageState::Missing ||
        storage == StorageState::Invalid ||
        storage == StorageState::ExistingEmptyFile) {
        throw std::runtime_error("Unable to read user data.");
    }

    ScopedStdoutSilencer silenceStdout;
    DataManager dataManager(filename_);
    if (storage == StorageState::ExistingNonEmptyFile && dataManager.GetAllUsers().empty()) {
        throw std::runtime_error("Unable to read user data.");
    }
    return dataManager.SetStrictMode(enabled);
}

} // namespace tundraux::backend

#include "data_manager_user_store.hpp"

#include "udata.hpp"

#include <utility>

namespace tundraux::backend {

namespace {

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
    DataManager dataManager(filename_);
    std::vector<BackendUser> out;
    for (const auto& user : dataManager.GetAllUsers()) {
        out.push_back(toBackendUser(user));
    }
    return out;
}

bool DataManagerUserStore::updateUser(const std::string& name, const BackendUser& user) {
    DataManager dataManager(filename_);
    return dataManager.UpdateUser(name, toLegacyUser(user));
}

} // namespace tundraux::backend

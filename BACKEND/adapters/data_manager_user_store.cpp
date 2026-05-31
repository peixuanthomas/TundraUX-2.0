#include "data_manager_user_store.hpp"

#include <utility>

namespace tundraux::backend {

DataManagerUserStore::DataManagerUserStore(std::string filename)
    : filename_(std::move(filename)) {}

BackendUser toBackendUser(const USER& user) {
    return BackendUser{
        user.type,
        user.name,
        user.password,
        user.password_hint,
        user.count
    };
}

USER toLegacyUser(const BackendUser& user) {
    return USER{
        user.type,
        user.name,
        user.password,
        user.passwordHint,
        user.failedCount
    };
}

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

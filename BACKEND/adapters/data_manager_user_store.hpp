#pragma once

#include "user_store.hpp"
#include "udata.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class DataManagerUserStore final : public UserStore {
public:
    explicit DataManagerUserStore(std::string filename);

    std::vector<BackendUser> listUsers() const override;
    bool updateUser(const std::string& name, const BackendUser& user) override;

private:
    std::string filename_;
};

BackendUser toBackendUser(const USER& user);
USER toLegacyUser(const BackendUser& user);

} // namespace tundraux::backend

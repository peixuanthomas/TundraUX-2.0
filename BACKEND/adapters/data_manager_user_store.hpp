#pragma once

#include "user_store.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class DataManagerUserStore final : public UserStore {
public:
    explicit DataManagerUserStore(std::string filename);

    std::vector<BackendUser> listUsers() const override;
    bool isStoreEmpty() const override;
    bool addUser(const BackendUser& user) override;
    bool updateUser(const std::string& name, const BackendUser& user) override;
    bool removeUser(const std::string& name) override;
    bool getStrictMode() const override;
    bool setStrictMode(bool enabled) override;

private:
    std::string filename_;
};

} // namespace tundraux::backend

#pragma once

#include <string>
#include <vector>

struct USER {
    std::string type;
    std::string name;
    std::string password;
    std::string password_hint;
    int count;
};

class DataManager {
    public:
        DataManager(const std::string& filename);
        bool AddUser(const USER& user);
        bool UpdateUser(const std::string& name, const USER& updatedUser);
        bool RemoveUser(const std::string& name);
        const std::vector<USER>& GetAllUsers() const;
        bool GetStrictMode() const;
        bool SetStrictMode(bool enabled);
    private:
        std::vector<USER> userDataList;
        bool strictMode_ = false;
        std::string filename_;
        void LoadUsersFromFile();
        bool SaveUsersToFile();
};

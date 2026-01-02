#ifndef UDATA_H
#define UDATA_H

#include <string>
#include <vector>


struct USER {
    std::string type;
    std::string name;
    std::string password;
    std::string password_hint;
    int count;
};

void createfile();
std::string encryptDecrypt(const std::string& input);

class DataManager {
    public:
        DataManager(const std::string& filename);
        bool AddUser(const USER& user);
        bool UpdateUser(const std::string& name, const USER& updatedUser);
        bool RemoveUser(const std::string& name);
        bool ComparePassword(const std::string& name, const std::string& password);
        const std::vector<USER>& GetAllUsers() const;
        const std::vector<std::string> GetAllUsernames() const;
    private:
        std::vector<USER> userDataList;
        std::string filename_;
        void LoadUsersFromFile();
        void SaveUsersToFile();
};
void ReadOldFile();

#endif // !UDATA_H
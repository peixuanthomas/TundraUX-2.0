#include "udata.hpp"
#include <fstream>
#include "color.hpp"
#include <string>
#include "crypto.hpp" //offers encrypt/decrypt functions
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <utility>
#include <windows.h>

//Old modules just for reading old files.
//ATTENTION: These modules should not be used in new code.

static bool readEncryptedString(std::ifstream& in, std::string& data);
static void read_info();

namespace {
constexpr size_t MAX_USER_COUNT = 10000;
constexpr size_t MAX_USER_STRING_LENGTH = 1024 * 1024;
constexpr std::uintmax_t MAX_USER_DATA_FILE_SIZE = 64 * 1024 * 1024;

bool readExact(std::ifstream& file, void* data, std::streamsize size, const std::string& label) {
    file.read(reinterpret_cast<char*>(data), size);
    if (!file) {
        colorcout("red", "Error: Failed to read " + label + " from file.\n");
        return false;
    }
    return true;
}

bool readStoredString(
    std::ifstream& file,
    std::string& value,
    const std::string& label,
    bool encrypted = false
) {
    size_t length = 0;
    if (!readExact(file, &length, sizeof(length), label + " length")) {
        return false;
    }
    if (length > MAX_USER_STRING_LENGTH) {
        colorcout("red", "Error: " + label + " exceeds maximum length.\n");
        return false;
    }

    std::string buffer;
    try {
        buffer.resize(length);
    } catch (const std::exception&) {
        colorcout("red", "Error: Unable to allocate memory for " + label + ".\n");
        return false;
    }

    if (length > 0 && !readExact(file, buffer.data(), static_cast<std::streamsize>(length), label)) {
        return false;
    }

    if (encrypted) {
        try {
            value = decrypt(buffer);
        } catch (const std::exception&) {
            colorcout("red", "Error: Failed to decrypt " + label + ".\n");
            return false;
        }
    } else {
        value = std::move(buffer);
    }
    return true;
}
}

std::string encryptDecrypt(const std::string& input) {
    const char key = 0x55;  // Fixed key
    std::string output = input;
    for (size_t i = 0; i < output.length(); ++i) {
        output[i] ^= key;  // XOR operation
    }
    return output;
}
static bool readEncryptedString(std::ifstream& in, std::string& data) {
    size_t len;
    in.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (!in) return false;
    std::vector<char> buffer(len);
    in.read(buffer.data(), len);
    if (!in) return false;
    data = encryptDecrypt(std::string(buffer.data(), len));
    return true;
}
struct OLD_USER {
    std::string name;
    std::string password;
    std::string passwordtip;
    std::string activationCode;
    int count;
};
static OLD_USER currentUser;
static void read_info() {
    std::ifstream in("user_data", std::ios::binary);
    if (!in) {
        colorcout("red", "Error: User data file not found or unreadable.\n");
        currentUser = {};
        return;
    }
    std::string oldName = currentUser.name;
    if (!readEncryptedString(in, currentUser.name)) {
        currentUser = {};
        return;
    }
    if (!readEncryptedString(in, currentUser.password)) {
        currentUser = {};
        return;
    }
    if (!readEncryptedString(in, currentUser.passwordtip)) {
        currentUser = {};
        return;
    }
    if (!readEncryptedString(in, currentUser.activationCode)) {
        currentUser = {};
        return;
    }
    int encryptedCount;
    in.read(reinterpret_cast<char*>(&encryptedCount), sizeof(encryptedCount));
    if (!in) {
        currentUser = {};
        return;
    }
    currentUser.count = encryptedCount ^ 0xAA55AA55;
}
//End of old modules.
void ReadOldFile() {
    read_info();
    if (currentUser.name.empty()) {
        colorcout("red", "No valid username found.\n");
        return;
    }
    if(currentUser.password.empty()) {
        colorcout("red", "No valid password found.\n");
        return;
    }
    if(currentUser.count > 7 || currentUser.count < 0) {
        colorcout("red", "Found invalid user, process terminated.\n");
        return;
    }
    colorcout("white", "Please confirm the imported user details:\n\n");
    colorcout("white", "Username: " + currentUser.name + "\n");
    colorcout("white", "Password: " + currentUser.password + "\n");
    colorcout("white", "Password Hint: " + (currentUser.passwordtip.empty() ? "(none)" : currentUser.passwordtip) + "\n\n");
    if(!getYN("Are these details correct?")) {
        colorcout("white", "User data import cancelled.\n");
        return;
    }
    DataManager dataManager("user_data.dat");
    USER newUser;
    newUser.type = "admin";
    newUser.name = currentUser.name;
    newUser.password = currentUser.password;
    newUser.password_hint = currentUser.passwordtip;
    newUser.count = currentUser.count;
    if(dataManager.AddUser(newUser)) {
        colorcout("green", "User data imported successfully!\n");
        if (getYN("Delete old user data file?")) std::remove("user_data");
    } else {
        colorcout("red", "User already exists.\n");
    }
}

/*
多用户数据存储/读取设计说明：
1) 内存模型：
   - 使用全局静态 std::vector<USERINFO> userDataList 存放所有用户。
   - 访问入口：提供 getUserDataList()（只读）与非 const 的访问器/修改器（如 add/update/remove）。

2) 文件格式：
   - 采用二进制文件保存，文件名为 "users.dat"。
   - 文件头包含当前程序的版本号和用户数量。此程序版本号为 2。
   - 每个用户记录依次存储 USERINFO 结构的字段，字符串字段前置长度信息（size_t）。
   - 用户密码必须加密存储，不可以使用以前的 encryptDecrypt 函数。

3) 读取流程（LoadUsersFromFile）：
   - 打开文件（std::ifstream, binary）。
   - 验证版本；失败则报错返回。此程序版本为 2。
   - 验证是否存在admin用户，若无则报错返回。
   - 读取用户数量 N，预分配 vector。
   - 循环读取每个 USERINFO 的字段（name/password/password_hint/count/...），逐条 push_back。
   - 校验 I/O 状态，失败则清空并报错。

4) 写入流程（SaveUsersToFile）：
   - 打开文件（std::ofstream, binary | trunc）。
   - 写入版本，再写入用户数量 N。
   - 逐个写入 USERINFO 字段，确保长度在合理范围内。
   - 刷新/关闭，检查 failbit/badbit。
   - 写入时采用临时文件 + 原子替换，降低写半截的风险。

5) 基本操作函数（供业务层调用）：
    - AddUser(const USER& user)：添加新用户，检查重名。
    - UpdateUser(const std::string& name, const USER& updatedUser)：更新指定用户信息。
    - RemoveUser(const std::string& name)：删除指定用户。
    - ComparePassword(const std::string& name, const std::string& password)：验证用户名密码。
    - GetAllUsers()：返回用户列表引用（只读）。
    - GetAllUsernames()：返回用户名列表（只读）。
*/
DataManager::DataManager(const std::string& filename) : filename_(filename) {
    LoadUsersFromFile();
}

bool DataManager::AddUser(const USER& user) {
    for (const auto& u : userDataList) {
        if (u.name == user.name) {
            return false; // User already exists
        }
    }
    userDataList.push_back(user);
    if (!SaveUsersToFile()) {
        userDataList.pop_back();
        return false;
    }
    return true;
}

bool DataManager::UpdateUser(const std::string& name, const USER& updatedUser) {
    for (auto& u : userDataList) {
        if (u.name == name) {
            const USER previousUser = u;
            u = updatedUser;
            if (!SaveUsersToFile()) {
                u = previousUser;
                return false;
            }
            return true;
        }
    }
    return false;
}

bool DataManager::RemoveUser(const std::string& name) {
    auto it = std::find_if(userDataList.begin(), userDataList.end(),
        [&name](const USER& u) { return u.name == name; });
    if (it == userDataList.end()) {
        return false;
    }

    const auto index = static_cast<std::vector<USER>::difference_type>(
        std::distance(userDataList.begin(), it));
    const USER removedUser = *it;
    userDataList.erase(it);
    if (!SaveUsersToFile()) {
        userDataList.insert(userDataList.begin() + index, removedUser);
        return false;
    }
    return true;
}

bool DataManager::ComparePassword(const std::string& name, const std::string& password) {
    const std::string encryptedInput = encrypt(password);
    for (const auto& u : userDataList) {
        if (u.name == name) {
            const std::string encryptedStored = encrypt(u.password); // Avoid decrypting stored password
            return encryptedInput == encryptedStored;
        }
    }
    return false;
}

const std::vector<USER>& DataManager::GetAllUsers() const {
    return userDataList;
}

const std::vector<std::string> DataManager::GetAllUsernames() const {
    std::vector<std::string> usernames;
    for (const auto& u : userDataList) {
        usernames.push_back(u.name);
    }
    return usernames;
}

void DataManager::LoadUsersFromFile() {
    std::ifstream inFile(filename_, std::ios::binary);
    if (!inFile) {
        // Exit if file does not exist
        return;
    }

    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(filename_, sizeError);
    if (!sizeError && fileSize > MAX_USER_DATA_FILE_SIZE) {
        colorcout("red", "Error: User data file is too large.\n");
        return;
    }

    int version = 0;
    if (!readExact(inFile, &version, sizeof(version), "user data version")) {
        return;
    }
    if (version != 2) {
        colorcout("red", "Error: Unsupported user data file version.\n");
        return;
    }

    size_t userCount = 0;
    if (!readExact(inFile, &userCount, sizeof(userCount), "user count")) {
        return;
    }
    if (userCount > MAX_USER_COUNT) {
        colorcout("red", "Error: User count exceeds maximum supported value.\n");
        return;
    }

    std::vector<USER> tempUserDataList;
    tempUserDataList.reserve(userCount);
    for (size_t i = 0; i < userCount; ++i) {
        USER user;

        if (!readStoredString(inFile, user.type, "user type") ||
            !readStoredString(inFile, user.name, "user name") ||
            !readStoredString(inFile, user.password, "user password", true) ||
            !readStoredString(inFile, user.password_hint, "password hint") ||
            !readExact(inFile, &user.count, sizeof(user.count), "failed login count")) {
            return;
        }

        tempUserDataList.push_back(user);
    }

    bool hasAdmin = false;
    for (const auto& user : tempUserDataList) {
        if (user.type == "admin") {
            hasAdmin = true;
            break;
        }
    }
    if (!hasAdmin) {
        colorcout("red", "Error: No admin user found in user data file.\n");
        return;
    }

    userDataList = std::move(tempUserDataList);
}

bool DataManager::SaveUsersToFile() {
    std::string tempFilename = filename_ + ".tmp";

    //Check if file exists and is writable
    std::error_code existsError;
    if(!std::filesystem::exists(filename_, existsError) || existsError) {
        colorcout("red", "Error: User data file does not exist or is not writable.\n");
        return false;
    }
    
    std::ofstream outFile(tempFilename, std::ios::binary | std::ios::trunc);
    if (!outFile) {
        colorcout("red", "Error: Unable to open temporary file for writing.\n");
        return false;
    }

    int version = 2;
    outFile.write(reinterpret_cast<const char*>(&version), sizeof(version));

    size_t userCount = userDataList.size();
    outFile.write(reinterpret_cast<const char*>(&userCount), sizeof(userCount));

    for (const auto& user : userDataList) {
        size_t length;

        // Write type
        length = user.type.size();
        if (length > MAX_USER_STRING_LENGTH) {
            colorcout("red", "Error: User type exceeds maximum length.\n");
            outFile.close();
            std::remove(tempFilename.c_str());
            return false;
        }
        outFile.write(reinterpret_cast<const char*>(&length), sizeof(length));
        outFile.write(user.type.data(), length);

        // Write name
        length = user.name.size();
        if (length > MAX_USER_STRING_LENGTH) {
            colorcout("red", "Error: User name exceeds maximum length.\n");
            outFile.close();
            std::remove(tempFilename.c_str());
            return false;
        }
        outFile.write(reinterpret_cast<const char*>(&length), sizeof(length));
        outFile.write(user.name.data(), length);

        // Write encrypted password
        std::string encryptedPassword = encrypt(user.password);
        length = encryptedPassword.size();
        if (length > MAX_USER_STRING_LENGTH) {
            colorcout("red", "Error: Encrypted password exceeds maximum length.\n");
            outFile.close();
            std::remove(tempFilename.c_str());
            return false;
        }
        outFile.write(reinterpret_cast<const char*>(&length), sizeof(length));
        outFile.write(encryptedPassword.data(), length);

        // Write password hint
        length = user.password_hint.size();
        if (length > MAX_USER_STRING_LENGTH) {
            colorcout("red", "Error: Password hint exceeds maximum length.\n");
            outFile.close();
            std::remove(tempFilename.c_str());
            return false;
        }
        outFile.write(reinterpret_cast<const char*>(&length), sizeof(length));
        outFile.write(user.password_hint.data(), length);

        // Write count
        outFile.write(reinterpret_cast<const char*>(&user.count), sizeof(user.count));
    }

    outFile.flush();
    if (outFile.fail() || outFile.bad()) {
        colorcout("red", "Error: Failed to write user data to temporary file.\n");
        outFile.close();
        std::remove(tempFilename.c_str());
        return false;
    }
    
    outFile.close();

    if (!MoveFileExA(
            tempFilename.c_str(),
            filename_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        colorcout("red", "Error: Failed to replace user data file. Windows error: " +
            std::to_string(GetLastError()) + "\n");
        std::remove(tempFilename.c_str());
        return false;
    }

    return true;
}

void createfile() {
    std::ofstream file("user_data.dat", std::ios::binary);
    if (!file) {
        colorcout("red", "Error: Unable to create user data file.\n");
        return;
    }

    int version = 2;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    size_t userCount = 1;
    file.write(reinterpret_cast<const char*>(&userCount), sizeof(userCount));
    USER placeholder = {
        "admin",
        "null",
        "null",
        "Default placeholder user, should not appear in normal usage.",
        0
    };
    auto writeString = [&file](const std::string& str) {
        size_t length = str.size();
        file.write(reinterpret_cast<const char*>(&length), sizeof(length));
        file.write(str.data(), length);
    };

    writeString(placeholder.type);
    writeString(placeholder.name);
    writeString(placeholder.password);
    writeString(placeholder.password_hint);
    file.write(reinterpret_cast<const char*>(&placeholder.count), sizeof(placeholder.count));

    file.close();
    colorcout("green", "Data file created successfully.\n");
}

void listUser() {
    DataManager dataManager("user_data.dat");
    const auto& users = dataManager.GetAllUsers();
    if (users.empty()) {
        colorcout("yellow", "No users found.\n");
        return;
    }
    colorcout("cyan", "Current Users:\n");
    for (const auto& user : users) {
        colorcout("white", "Username: " + user.name + "\n");
    }
}

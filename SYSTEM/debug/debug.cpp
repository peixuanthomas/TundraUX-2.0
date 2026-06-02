#include "debug.hpp"
#include "build_info.hpp"
#include "color.hpp"
#include "hello.hpp"
#include <string>
#include "crypto.hpp"
#include <cstdio>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

namespace {
constexpr size_t MAX_USER_COUNT = 10000;
constexpr size_t MAX_USER_STRING_LENGTH = 1024 * 1024;

bool usesBackend(bool backendMode) {
    return backendMode;
}

void printBackendModeDisabledMessage(const char* command) {
    colorcout(
        "red",
        std::string(command) +
        " is unavailable in backend mode; use --legacy-direct for local file debugging.\n"
    );
}

void print_display_test_line(const std::string& colorName) {
    colorcout(colorName, "Display test: " + colorName + "\n");
}
}

void delete_file() {
    if(std::remove("user_data.dat") == 0) {
        colorcout("green", "User data file deleted successfully.\n");
    } else {
        colorcout("red", "Error deleting user data file or file does not exist.\n");
    }
}

//List the whole structure of user_data.dat for debugging
void struct_file() {
    std::ifstream in("user_data.dat", std::ios::binary);
    if (!in) {
        colorcout("red", "Error: Unable to open user_data.dat\n");
        return;
    }
    auto readString = [&](std::string& out) -> bool {
        size_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!in) return false;
        if (len > MAX_USER_STRING_LENGTH) {
            colorcout("red", "Error: User string length exceeds maximum supported value\n");
            return false;
        }
        try {
            out.assign(len, '\0');
        } catch (const std::exception&) {
            colorcout("red", "Error: Unable to allocate memory for user string\n");
            return false;
        }
        if (len > 0) in.read(&out[0], static_cast<std::streamsize>(len));
        return static_cast<bool>(in);
    };
    int version = 0;
    size_t userCount = 0;
    std::uint8_t strictValue = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!in) {
        colorcout("red", "Error: Failed to read header\n");
        return;
    }

    if (version != 21) {
        colorcout("red", "Error: Unsupported user_data.dat version in struct_file\n");
        return;
    }
    in.read(reinterpret_cast<char*>(&strictValue), sizeof(strictValue));
    in.read(reinterpret_cast<char*>(&userCount), sizeof(userCount));
    if (!in) {
        colorcout("red", "Error: Failed to read header\n");
        return;
    }
    if (strictValue != 0 && strictValue != 1) {
        colorcout("red", "Error: Invalid strict flag in user_data.dat header\n");
        return;
    }
    std::cout << version << '\n';
    std::cout << static_cast<int>(strictValue) << '\n';
    std::cout << userCount << '\n';

    if (userCount > MAX_USER_COUNT) {
        colorcout("red", "Error: User count exceeds maximum supported value\n");
        return;
    }

    for (size_t i = 0; i < userCount; ++i) {
        std::string type, name, encPass, hint;
        if (!readString(type) || !readString(name) || !readString(encPass) || !readString(hint)) {
            colorcout("red", "Error: Failed to read user block\n");
            return;
        }
        int count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in) {
            colorcout("red", "Error: Failed to read count\n");
            return;
        }
        const std::string pass = decrypt(encPass);
        std::cout << type << '\n';
        std::cout << name << '\n';
        std::cout << pass << '\n';
        std::cout << hint << '\n';
        std::cout << count;
        if (i + 1 != userCount) {
            std::cout << '\n';
        }
    }
}

void display_test(const std::string& colorName) {
    if (!colorName.empty()) {
        if (!hasConsoleColor(colorName)) {
            colorcout("red", "No such color: " + colorName + "\n");
            return;
        }
        print_display_test_line(colorName);
        return;
    }

    for (const auto& color : getDisplayTestColorNames()) {
        print_display_test_line(color);
    }
}

void license() {
    std::ifstream licenseFile("license");
    if (!licenseFile) {
        colorcout("red", "License file not found.\n");
        return;
    }
    std::string line;
    while (std::getline(licenseFile, line)) {
        colorcout("white", line + "\n");
    }
    licenseFile.close();
}

void handleLicenseCommand(const std::string&) {
    license();
}

void handleDisplayTestCommand(const std::string& input) {
    std::istringstream iss(input);
    std::string commandToken;
    std::string colorName;
    std::string extra;
    iss >> commandToken >> colorName;
    if (iss >> extra) {
        colorcout("red", "Usage: displaytest [color]\n");
        return;
    }
    display_test(colorName);
}

void handleDebugCreateFileCommand(const std::string&, bool backendMode) {
    if (usesBackend(backendMode)) {
        printBackendModeDisabledMessage("dbg:createfile");
        return;
    }

    createfile();
    USER debuguser;
    debuguser.type = "admin";
    debuguser.name = "Admin";
    debuguser.password = "";
    debuguser.password_hint = "Default admin user created by dbg:createfile command.";
    debuguser.count = 0;
    DataManager dm("user_data.dat");
    dm.AddUser(debuguser);
    debuguser.type = "user";
    debuguser.name = "User";
    debuguser.password = "";
    debuguser.password_hint = "Default regular user created by dbg:createfile command.";
    dm.AddUser(debuguser);
    dm.RemoveUser("null");
}

void handleDebugHelloCommand(const std::string&) {
    hello();
}

void handleDebugDeleteFileCommand(const std::string&, bool backendMode) {
    if (usesBackend(backendMode)) {
        printBackendModeDisabledMessage("dbg:deletefile");
        return;
    }

    delete_file();
}

void handleDebugStructFileCommand(const std::string&, bool backendMode) {
    if (usesBackend(backendMode)) {
        printBackendModeDisabledMessage("dbg:structfile");
        return;
    }

    struct_file();
}

void handleDebugEnvCommand(const std::string&) {
    dbg_env();
}

void handleDebugForceLoginCommand(
    const std::string& input,
    USER& currentUser,
    bool backendMode
) {
    if (usesBackend(backendMode)) {
        colorcout(
            "red",
            "dbg:forcelogin is unavailable in backend mode; use --legacy-direct for local file debugging.\n"
        );
        return;
    }

    std::istringstream iss(input);
    std::string cmd, username;
    iss >> cmd >> username;
    if (username.empty()) {
        colorcout("red", "Usage: dbg:forcelogin <username>\n");
        return;
    }
    DataManager dm("user_data.dat");
    const auto& users = dm.GetAllUsers();
    auto it = std::find_if(users.begin(), users.end(),
        [&](const USER& u){ return u.name == username; });
    if (it == users.end()) {
        colorcout("red", "[DBG] User not found: " + username + "\n");
        return;
    }
    currentUser = *it;
    colorcout("green", "[DBG] Force-logged in as: " + currentUser.name + " (" + currentUser.type + ")\n");
}

void dbg_env() {
    colorcout("cyan", "[DBG] Build timestamp : " + std::string(tundraux::build_info::timestamp()) + "\n");
#if defined(_MSC_VER)
    colorcout("cyan", "[DBG] Compiler        : MSVC " + std::to_string(_MSC_VER) + "\n");
#elif defined(__GNUC__)
    colorcout("cyan", "[DBG] Compiler        : GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "\n");
#else
    colorcout("cyan", "[DBG] Compiler        : Unknown\n");
#endif
#if defined(_WIN64)
    colorcout("cyan", "[DBG] Platform        : Windows 64-bit\n");
#elif defined(_WIN32)
    colorcout("cyan", "[DBG] Platform        : Windows 32-bit\n");
#else
    colorcout("cyan", "[DBG] Platform        : Unknown\n");
#endif
    colorcout("cyan", "[DBG] sizeof(USER)    : " + std::to_string(sizeof(USER)) + " bytes\n");
    colorcout("cyan", "[DBG] sizeof(size_t)  : " + std::to_string(sizeof(size_t)) + " bytes\n");
}

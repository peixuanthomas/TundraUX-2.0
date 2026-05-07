#include "debug.hpp"
#include "color.hpp"
#include "editor.hpp"
#include "hello.hpp"
#include <string>
#include "crypto.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

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
        out.resize(len);
        if (len > 0) in.read(&out[0], len);
        return in.good();
    };
    int version = 0;
    size_t userCount = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&userCount), sizeof(userCount));
    if (!in) {
        colorcout("red", "Error: Failed to read header\n");
        return;
    }
    colorcout("white", std::to_string(version) + "\n");
    colorcout("white", std::to_string(userCount) + "\n");
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
        colorcout("white", type + "\n");
        colorcout("white", name + "\n");
        colorcout("white", pass + "\n");
        colorcout("white", hint + "\n");
        colorcout("white", std::to_string(count) + (i + 1 == userCount ? "" : "\n"));
    }
}

void display_test() {
    colorcout("green", "Green Text\n");
    colorcout("red", "Red Text (Note: this should make a sound when displayed)\n");
    colorcout("blue", "Blue Text\n");
    colorcout("yellow", "Yellow Text\n");
    colorcout("cyan", "Cyan Text\n");
    colorcout("magenta", "Magenta Text\n");
    colorcout("white", "White Text\n");
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

void handleDisplayTestCommand(const std::string&) {
    display_test();
}

void handleDebugEditorCommand(const std::string& input) {
    std::istringstream iss(input);
    std::string commandToken, backend;
    iss >> commandToken >> backend;
    if (backend.empty()) {
        colorcout("cyan", "Editor backend: " + get_editor_backend_name() + "\n");
        colorcout("white", "Available backends: " + describe_editor_backend_options() + "\n");
        return;
    }
    if (!set_editor_backend_by_name(backend)) {
        colorcout("red", "Unknown or unavailable backend: " + backend + "\n");
        colorcout("white", "Available backends: " + describe_editor_backend_options() + "\n");
        return;
    }
    colorcout("green", "Editor backend set to: " + get_editor_backend_name() + "\n");
}

void handleDebugCreateFileCommand(const std::string&) {
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

void handleDebugDeleteFileCommand(const std::string&) {
    delete_file();
}

void handleDebugStructFileCommand(const std::string&) {
    struct_file();
}

void handleDebugEnvCommand(const std::string&) {
    dbg_env();
}

void handleDebugForceLoginCommand(const std::string& input, USER& currentUser) {
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
    constexpr const char* BUILD_TIME = __TIMESTAMP__;
    colorcout("cyan", "[DBG] Build timestamp : " + std::string(BUILD_TIME) + "\n");
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

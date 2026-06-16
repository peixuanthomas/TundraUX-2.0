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

std::string legacyUserDataPath() {
    return std::string("user_") + "data.dat";
}

bool usesBackend(bool backendMode) {
    return backendMode;
}

void print_display_test_line(const std::string& colorName) {
    colorcout(colorName, "Display test: " + colorName + "\n");
}
}

void delete_file() {
    const std::string path = legacyUserDataPath();
    if(std::remove(path.c_str()) == 0) {
        colorcout("green", "User data file deleted successfully.\n");
    } else {
        colorcout("red", "Error deleting user data file or file does not exist.\n");
    }
}

// List the whole structure of the legacy user data file for debugging.
void struct_file() {
    std::ifstream in(legacyUserDataPath(), std::ios::binary);
    if (!in) {
        colorcout("red", "Error: Unable to open legacy user data file\n");
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
        colorcout("red", "Error: Unsupported legacy user data version in struct_file\n");
        return;
    }
    in.read(reinterpret_cast<char*>(&strictValue), sizeof(strictValue));
    in.read(reinterpret_cast<char*>(&userCount), sizeof(userCount));
    if (!in) {
        colorcout("red", "Error: Failed to read header\n");
        return;
    }
    if (strictValue != 0 && strictValue != 1) {
        colorcout("red", "Error: Invalid strict flag in legacy user data header\n");
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
    colorcout(
        backendMode ? "yellow" : "red",
        backendMode
            ? "dbg:createfile is not available in backend-separated mode. Use setup flow or user management.\n"
            : "Backend unavailable. dbg:createfile cannot run in this build.\n"
    );
}

void handleDebugHelloCommand(const std::string&) {
    hello();
}

void handleDebugDeleteFileCommand(const std::string&, bool backendMode) {
    colorcout(
        backendMode ? "yellow" : "red",
        backendMode
            ? "dbg:deletefile is not available in backend-separated mode. Stop the backend and remove test data from the configured workspace.\n"
            : "Backend unavailable. dbg:deletefile cannot run in this build.\n"
    );
}

void handleDebugStructFileCommand(const std::string&, bool backendMode) {
    if (usesBackend(backendMode)) {
        colorcout(
            "yellow",
            "dbg:structfile is not available in backend-separated mode. Inspect backend storage outside the frontend.\n"
        );
        return;
    }

    struct_file();
}

void handleDebugEnvCommand(const std::string&) {
    dbg_env();
}

void handleDebugForceLoginCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
    if (backendRuntime == nullptr || backendRuntime->client() == nullptr) {
        colorcout("red", "Backend unavailable. Debug force-login requires backend mode.\n");
        return;
    }

    std::istringstream iss(input);
    std::string cmd, username;
    iss >> cmd >> username;
    if (username.empty()) {
        colorcout("red", "Usage: dbg:forcelogin <username>\n");
        return;
    }

    tundraux::frontend::BackendFacade facade(*backendRuntime);
    const auto result = facade.debugForceLogin(username);
    if (!result.ok) {
        colorcout("red", (result.message.empty() ? "Debug force-login failed." : result.message) + "\n");
        return;
    }
    currentUser = result.value;
    colorcout("green", "Debug force-login: " + currentUser.name + " (" + currentUser.type + ")\n");
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
    colorcout("cyan", "[DBG] current user DTO: ShellUser\n");
    colorcout("cyan", "[DBG] sizeof(size_t)  : " + std::to_string(sizeof(size_t)) + " bytes\n");
}

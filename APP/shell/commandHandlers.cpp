#include "commandHandlers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cwctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

#include "color.hpp"
#include "editor.hpp"
#include "manageusers.hpp"
#include "TUXfile.hpp"
#include "explorer.hpp"

namespace {
namespace fs = std::filesystem;

fs::path normalizeExistingPath(const fs::path& path) {
    std::error_code error;
    fs::path normalized = fs::weakly_canonical(path, error);
    if (error) {
        normalized = fs::absolute(path, error);
    }
    if (error) {
        normalized = path;
    }
    return normalized.lexically_normal();
}

std::wstring normalizedPart(const fs::path& path) {
    std::wstring value = path.wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch == L'/') {
            return L'\\';
        }
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool isPathInsideRoot(const fs::path& candidate, const fs::path& root) {
    const fs::path candidatePath = normalizeExistingPath(candidate);
    const fs::path rootPath = normalizeExistingPath(root);
    auto candidateIt = candidatePath.begin();

    for (auto rootIt = rootPath.begin(); rootIt != rootPath.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidatePath.end()) {
            return false;
        }
        if (normalizedPart(*candidateIt) != normalizedPart(*rootIt)) {
            return false;
        }
    }

    return true;
}

bool hasUnsafePathPart(const fs::path& path) {
    for (const auto& part : path) {
        const std::string value = part.u8string();
        if (value == "." || value == "..") {
            return true;
        }
    }
    return false;
}
}

void handleLoginCommand(const std::string& input, USER& currentUser) {
    std::istringstream iss(input);
    std::string cmd, username;
    iss >> cmd >> username;
    if (username.empty())
    {
        colorcout("yellow", "Usage: login <username>\n");
        return;
    }
    DataManager dataManager("user_data.dat");
    const auto &users = dataManager.GetAllUsers();
    auto it = std::find_if(users.begin(), users.end(),
                           [&](const USER &u)
                           { return u.name == username; });

    if (it == users.end())
    {
        colorcout("red", "User not found: " + username + "\n");
        return;
    }
    // disable user when count > 7
    if (it->count > 7)
    {
        colorcout("red", "User disabled due to too many failed attempts.\n");
        return;
    }
    std::string password = getHiddenInput("Please enter password for user " + username + ": ", '*');
    if (dataManager.ComparePassword(username, password))
    {
        USER updated = *it;
        updated.count = 0; // reset fail count on success
        dataManager.UpdateUser(username, updated);
        currentUser = updated;
        //colorcout("green", "Login successful! Welcome, " + currentUser.name + ".\n");
        rollcout("green", "Welcome, " + currentUser.name + "!");
        //std::cout << std::endl;
    }
    else
    {
        USER updated = *it;
        updated.count += 1; // add fail count on failure
        dataManager.UpdateUser(username, updated);
        colorcout("red", "Incorrect password for user " + username + ".\n");
        colorcout("red", "Failed attempts: " + std::to_string(updated.count) + "\n");
        colorcout("blue", "Password Hint: " + (it->password_hint.empty() ? "(none)" : it->password_hint) + "\n");
    }
}

void handleExitCommand(const std::string&) {
    exit(0);
}

void handleImportDataCommand(const std::string&) {
    ReadOldFile();
}

void handleTimeCommand(const std::string&) {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::tm local_tm{};
    localtime_s(&local_tm, &tt);
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    colorcout("white", "Current time: " + oss.str() + "\n");
    colorcout("white", "Timestamp: " + std::to_string(ts) + "\n");
}

void handleModifyCommand(const std::string&, USER& currentUser) {
    if (currentUser.name.empty())
    {
        colorcout("yellow", "No user is currently logged in.\n");
        return;
    }
    colorcout("cyan", "Modify current user\n");
    DataManager dataManager("user_data.dat");
    bool changed = false;
    if (getYN("Change password?"))
    {
        while (true)
        {
            std::string newPwd = getHiddenInput("Enter new password: ", '*');
            if (newPwd.empty())
            {
                colorcout("red", "Password cannot be empty. Please try again.\n");
                continue;
            }
            if (newPwd.length() < 6)
            {
                colorcout("red", "Password must be at least 6 characters long. Please try again.\n");
                continue;
            }
            bool hasUpper = false, hasLower = false, hasDigit = false;
            for (char c : newPwd)
            {
                if (std::isupper(static_cast<unsigned char>(c)))
                    hasUpper = true;
                else if (std::islower(static_cast<unsigned char>(c)))
                    hasLower = true;
                else if (std::isdigit(static_cast<unsigned char>(c)))
                    hasDigit = true;
            }
            if (!(hasUpper && hasLower && hasDigit))
            {
                colorcout("red", "Password must contain at least one uppercase letter, one lowercase letter, and one digit. Please try again.\n");
                continue;
            }
            std::string confirm = getHiddenInput("Confirm new password: ", '*');
            if (newPwd != confirm)
            {
                colorcout("red", "Passwords do not match. Please try again.\n");
                continue;
            }
            currentUser.password = newPwd;
            changed = true;
            break;
        }
    }
    if (getYN("Change password hint?"))
    {
        while (true)
        {
            std::string newHint;
            colorcout("white", "Enter new password hint (leave blank if none):");
            std::getline(std::cin, newHint);
            if (newHint == currentUser.password)
            {
                colorcout("red", "Password hint cannot be the same as the password.\n");
            }
            else
            {
                currentUser.password_hint = newHint;
                changed = true;
                break;
            }
        }
    }
    if (changed)
    {
        if (dataManager.UpdateUser(currentUser.name, currentUser))
        {
            colorcout("green", "User info updated successfully.\n");
        }
        else
        {
            colorcout("red", "Failed to update user info.\n");
        }
    }
    else
    {
        colorcout("yellow", "No changes made.\n");
    }
}

void handleClearScreenCommand(const std::string&) {
    clear_screen();
    print_icon();
    std::cout << std::endl;
}

void handleLogoutCommand(const std::string&, USER& currentUser) {
    if (currentUser.name.empty())
    {
        colorcout("yellow", "No user is currently logged in.\n");
        return;
    }
    colorcout("green", "User " + currentUser.name + " logged out successfully.\n");
    currentUser = {
        "guest",
        "",
        "",
        "",
        0
    };
}

void handleListUserCommand(const std::string&) {
    listUser();
}

void handleTuxFileCommand(const std::string&, USER& currentUser) {
    file_editor(currentUser.name, currentUser.type);
}

void handleInfoCommand(const std::string&) {
    constexpr const char* BUILD = "Build: " __TIMESTAMP__;
    colorcout("cyan", "TundraUX 2.0 " + std::string(BUILD) + "\n");
}

void handleManageUsersCommand(const std::string&) {
    manage_users();
}

void handleEditCommand(const std::string& input) {
    std::istringstream iss(input);
    std::string cmd, filename;
    iss >> cmd >> filename;
    if (filename.empty()) {
        run_editor("", "");
        return;
    }
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lowerFilename.size() >= 4 &&
        lowerFilename.substr(lowerFilename.size() - 4) == ".tux") {
        colorcout("yellow", "Use TUXfile to open .TUX files.\n");
        return;
    }
    fs::path requestedPath(filename);
    if (requestedPath.is_absolute() || hasUnsafePathPart(requestedPath)) {
        colorcout("red", "Access Denied.\n");
        return;
    }

    const fs::path filesRoot = normalizeExistingPath(fs::current_path() / "Files");
    const fs::path targetPath = normalizeExistingPath(filesRoot / requestedPath);
    if (!isPathInsideRoot(targetPath, filesRoot)) {
        colorcout("red", "Access Denied.\n");
        return;
    }

    struct stat st;
    const std::string path = targetPath.string();
    if (stat(path.c_str(), &st) != 0) {
        colorcout("red", "Error: File not found: " + path + "\n");
        return;
    }
    run_editor(path, filename);
}

void handleExplorerCommand(const std::string&, USER& currentUser) {
    open_explorer(currentUser.name, currentUser.type);
}

void handleWhoamiCommand(const USER& currentUser) {
    if(currentUser.name.empty()) {
        colorcout("yellow", "No user is currently logged in.\n");
    } else {
        colorcout("white", "Current user: " + currentUser.name + " (" + currentUser.type + ")\n");
    }
}

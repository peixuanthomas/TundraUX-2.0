#include "hello.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

#include "color.hpp"
#include "udata.hpp"

namespace {

void printSetupHeader(const std::string& stepLabel, const std::string& title) {
    clear_screen();
    set_title("TundraUX 2.0 First Setup");
    colorcout("cyan", " TundraUX 2.0 first-time setup\n");
    colorcout("white", "Create the first administrator account before using TundraUX.\n");
    colorcout("yellow", "Passwords are hidden while typing. Use 'n' on a confirmation prompt to edit.\n\n");
    colorcout("cyan", stepLabel + " - " + title + "\n");
    colorcout("cyan", "--------------------------------------------\n");
}

std::string validateUsername(const std::string& username) {
    if (username.empty()) {
        return "Username cannot be empty.";
    }
    if (username == "null") {
        return "\"null\" is a reserved username. Please choose a different username.";
    }
    const bool hasWhitespace = std::any_of(username.begin(), username.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    if (hasWhitespace) {
        return "Username cannot contain spaces. Use login <username> after setup.";
    }
    return "";
}

std::string validatePassword(const std::string& password) {
    if (password.empty()) {
        return "Password cannot be empty.";
    }
    if (password.length() < 6) {
        return "Password must be at least 6 characters long.";
    }

    bool hasUpper = false;
    bool hasLower = false;
    bool hasDigit = false;
    for (char c : password) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            hasUpper = true;
        } else if (std::islower(static_cast<unsigned char>(c))) {
            hasLower = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            hasDigit = true;
        }
    }

    if (!(hasUpper && hasLower && hasDigit)) {
        return "Password must contain uppercase, lowercase, and numeric characters.";
    }
    return "";
}

void printPasswordRules() {
    colorcout("white", "Password requirements:\n");
    colorcout("white", "  - At least 6 characters\n");
    colorcout("white", "  - At least 1 uppercase letter\n");
    colorcout("white", "  - At least 1 lowercase letter\n");
    colorcout("white", "  - At least 1 number\n\n");
}

std::string maskedPasswordSummary(const std::string& password) {
    return std::string(password.length(), '*') + " (" + std::to_string(password.length()) + " characters)";
}

std::string readUsername() {
    printSetupHeader("Step 1 of 4", "Administrator username");
    colorcout("white", "Choose the account name for the first administrator.\n");
    colorcout("yellow", "This name cannot be changed later and cannot contain spaces.\n\n");

    while (true) {
        std::string username;
        colorcout("white", "Username: ");
        std::getline(std::cin, username);

        const std::string error = validateUsername(username);
        if (!error.empty()) {
            colorcout("red", error + "\n");
            continue;
        }

        if (getYN("Use username \"" + username + "\"?")) {
            return username;
        }
        colorcout("yellow", "Okay, enter a different username.\n");
    }
}

std::string readPassword() {
    printSetupHeader("Step 2 of 4", "Administrator password");
    printPasswordRules();

    while (true) {
        const std::string password = getHiddenInput("Password: ", '*');
        const std::string error = validatePassword(password);
        if (!error.empty()) {
            colorcout("red", error + " Please try again.\n");
            continue;
        }

        const std::string confirm = getHiddenInput("Confirm password: ", '*');
        if (password != confirm) {
            colorcout("red", "Passwords do not match. Please try again.\n");
            continue;
        }

        return password;
    }
}

std::string readPasswordHint(const std::string& password) {
    printSetupHeader("Step 3 of 4", "Password hint");
    colorcout("white", "Add an optional hint for failed login attempts.\n");
    colorcout("yellow", "Do not enter the password itself. Press Enter to skip.\n\n");

    while (true) {
        std::string passwordHint;
        colorcout("white", "Password hint: ");
        std::getline(std::cin, passwordHint);

        if (passwordHint == password) {
            colorcout("red", "Password hint cannot be the same as the password.\n");
            continue;
        }
        return passwordHint;
    }
}

bool confirmDetails(
    const std::string& username,
    const std::string& password,
    const std::string& passwordHint
) {
    printSetupHeader("Step 4 of 4", "Review and create");
    colorcout("white", "Username: " + username + "\n");
    colorcout("white", "Role: admin\n");
    colorcout("white", "Password: " + maskedPasswordSummary(password) + "\n");
    colorcout("white", "Password hint: " + (passwordHint.empty() ? "(none)" : passwordHint) + "\n\n");
    colorcout("yellow", "Press y to create this account, or n to re-enter the details.\n");
    return getYN("Create administrator account now?");
}

bool createAdminUser(
    const std::string& username,
    const std::string& password,
    const std::string& passwordHint
) {
    createfile();
    if (!std::ifstream("user_data.dat")) {
        colorcout("red", "Unable to create user_data.dat. Setup cannot continue.\n");
        return false;
    }

    USER newUser;
    newUser.type = "admin";
    newUser.name = username;
    newUser.password = password;
    newUser.password_hint = passwordHint;
    newUser.count = 0;

    DataManager dataManager("user_data.dat");
    if (!dataManager.AddUser(newUser)) {
        colorcout("red", "Unable to create user. The username may already exist.\n");
        return false;
    }

    dataManager.RemoveUser("null");
    return true;
}

}

void hello() {
    while (true) {
        const std::string username = readUsername();
        const std::string password = readPassword();
        const std::string passwordHint = readPasswordHint(password);

        if (!confirmDetails(username, password, passwordHint)) {
            colorcout("yellow", "Restarting setup so you can edit the details.\n");
            Sleep(1000);
            continue;
        }

        if (!createAdminUser(username, password, passwordHint)) {
            pause();
            continue;
        }

        colorcout("green", "User created successfully!\n");
        pause();
        return;
    }
}

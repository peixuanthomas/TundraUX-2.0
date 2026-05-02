#include "hello.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

#include <conio.h>

#include "color.hpp"
#include "udata.hpp"

namespace {

enum class ReviewChoice {
    Create,
    EditUsername,
    EditPassword,
    EditHint
};

struct PasswordStatus {
    bool hasMinLength;
    bool hasUpper;
    bool hasLower;
    bool hasDigit;
};

void printSetupHeader(const std::string& stepLabel, const std::string& title) {
    clear_screen();
    set_title("TundraUX 2.0 init");
    colorcout("cyan", " TundraUX 2.0 first-time setup\n");
    colorcout("white", "Create the first administrator account before using TundraUX.\n");
    colorcout("yellow", "Passwords are hidden while typing. Use the review menu to edit before creating.\n\n");
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

PasswordStatus getPasswordStatus(const std::string& password) {
    PasswordStatus status = {
        password.length() >= 6,
        false,
        false,
        false
    };
    for (char c : password) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            status.hasUpper = true;
        } else if (std::islower(static_cast<unsigned char>(c))) {
            status.hasLower = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            status.hasDigit = true;
        }
    }
    return status;
}

bool isValidPassword(const PasswordStatus& status) {
    return status.hasMinLength && status.hasUpper && status.hasLower && status.hasDigit;
}

void printPasswordRule(const std::string& text, bool passed, bool highlightFailures) {
    colorcout(passed ? "green" : (highlightFailures ? "red" : "white"), "  - " + text + "\n");
}

void printPasswordRules(const std::string& password, bool highlightFailures) {
    const PasswordStatus status = getPasswordStatus(password);
    colorcout("white", "Password requirements:\n");
    printPasswordRule("At least 6 characters", status.hasMinLength, highlightFailures);
    printPasswordRule("At least 1 uppercase letter", status.hasUpper, highlightFailures);
    printPasswordRule("At least 1 lowercase letter", status.hasLower, highlightFailures);
    printPasswordRule("At least 1 number", status.hasDigit, highlightFailures);
    std::cout << std::endl;
}

void renderPasswordInput(
    const std::string& stepLabel,
    const std::string& password,
    bool highlightFailures,
    const std::string& message
) {
    printSetupHeader(stepLabel, "Administrator password");
    colorcout("white", "Type the password and press Enter when every requirement is green.\n");
    colorcout("yellow", "Backspace edits the password. The typed characters stay hidden.\n\n");
    colorcout("white", "Password: " + std::string(password.length(), '*') + "\n\n");
    printPasswordRules(password, highlightFailures);
    if (!message.empty()) {
        colorcout("red", message + "\n");
    }
}

std::string readPasswordWithLiveRules(const std::string& stepLabel) {
    std::string password;
    bool highlightFailures = false;
    std::string message;

    while (true) {
        renderPasswordInput(stepLabel, password, highlightFailures, message);

        const int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            const PasswordStatus status = getPasswordStatus(password);
            if (isValidPassword(status)) {
                std::cout << std::endl;
                return password;
            }

            highlightFailures = true;
            message = password.empty()
                ? "Password cannot be empty."
                : "Some password requirements are not met.";
            continue;
        }

        if (ch == 8) {
            if (!password.empty()) {
                password.pop_back();
            }
            message.clear();
            continue;
        }

        if (ch == 3) {
            password.clear();
            highlightFailures = true;
            message = "Password cleared.";
            continue;
        }

        if (ch == 0 || ch == 224) {
            _getch();
            continue;
        }

        if (std::isprint(static_cast<unsigned char>(ch))) {
            password.push_back(static_cast<char>(ch));
            message.clear();
        }
    }
}

std::string maskedPasswordSummary(const std::string& password) {
    return std::string(password.length(), '*') + " (" + std::to_string(password.length()) + " characters)";
}

std::string readUsername(const std::string& stepLabel = "Step 1 of 4") {
    printSetupHeader(stepLabel, "Administrator username");
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

std::string readPassword(const std::string& stepLabel = "Step 2 of 4") {
    while (true) {
        const std::string password = readPasswordWithLiveRules(stepLabel);
        const std::string confirm = getHiddenInput("Confirm password: ", '*');
        if (password != confirm) {
            colorcout("red", "Passwords do not match. Please try again.\n");
            Sleep(1000);
            continue;
        }

        return password;
    }
}

std::string readPasswordHint(const std::string& password, const std::string& stepLabel = "Step 3 of 4") {
    printSetupHeader(stepLabel, "Password hint");
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

ReviewChoice reviewDetails(
    const std::string& username,
    const std::string& password,
    const std::string& passwordHint
) {
    while (true) {
        printSetupHeader("Step 4 of 4", "Review and create");
        colorcout("white", "Username: " + username + "\n");
        colorcout("white", "Role: admin\n");
        colorcout("white", "Password: " + maskedPasswordSummary(password) + "\n");
        colorcout("white", "Password hint: " + (passwordHint.empty() ? "(none)" : passwordHint) + "\n\n");
        colorcout("yellow", "Choose what to do next:\n");
        colorcout("white", "  1 - Create account\n");
        colorcout("white", "  2 - Edit username\n");
        colorcout("white", "  3 - Edit password\n");
        colorcout("white", "  4 - Edit password hint\n\n");
        colorcout("white", "Press 1, 2, 3, or 4: ");

        const int selection = _getch();
        if (selection == 0 || selection == 224) {
            _getch();
        }
        if (std::isprint(static_cast<unsigned char>(selection))) {
            std::cout << static_cast<char>(selection) << std::endl;
        } else {
            std::cout << std::endl;
        }

        if (selection == '1' || selection == 'c' || selection == 'C') {
            return ReviewChoice::Create;
        }
        if (selection == '2' || selection == 'u' || selection == 'U') {
            return ReviewChoice::EditUsername;
        }
        if (selection == '3' || selection == 'p' || selection == 'P') {
            return ReviewChoice::EditPassword;
        }
        if (selection == '4' || selection == 'h' || selection == 'H') {
            return ReviewChoice::EditHint;
        }

        colorcout("red", "Invalid selection. Press 1, 2, 3, or 4.\n");
        Sleep(900);
    }
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
        std::string username = readUsername();
        std::string password = readPassword();
        std::string passwordHint = readPasswordHint(password);

        while (true) {
            const ReviewChoice choice = reviewDetails(username, password, passwordHint);
            if (choice == ReviewChoice::Create) {
                break;
            }
            if (choice == ReviewChoice::EditUsername) {
                username = readUsername("Edit");
            } else if (choice == ReviewChoice::EditPassword) {
                password = readPassword("Edit");
                if (passwordHint == password) {
                    colorcout("red", "The current password hint now matches the password. Please update it.\n");
                    Sleep(1200);
                    passwordHint = readPasswordHint(password, "Edit");
                }
            } else if (choice == ReviewChoice::EditHint) {
                passwordHint = readPasswordHint(password, "Edit");
            }
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

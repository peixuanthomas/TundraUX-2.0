#include "commandReg.hpp"

#include <algorithm>
#include <sstream>

#include "color.hpp"

bool hasCommandPermission(const std::string& requiredUserType, const std::string& currentUserType) {
    if (requiredUserType.empty()) {
        return true;
    }

    std::istringstream ss(requiredUserType);
    std::string userType;
    while (std::getline(ss, userType, ',')) {
        userType.erase(0, userType.find_first_not_of(" \t"));
        userType.erase(userType.find_last_not_of(" \t") + 1);
        if (userType == currentUserType) {
            return true;
        }
    }
    return false;
}

bool tryExecuteRegisteredCommand(
    const std::string& input,
    const std::vector<RegisteredCommand>& commands,
    const USER& currentUser
) {
    std::istringstream iss(input);
    std::string inputCommand;
    iss >> inputCommand;
    if (inputCommand.empty()) {
        return false;
    }

    for (const auto& cmd : commands) {
        bool matches = inputCommand == cmd.name;
        if (!matches) {
            matches = std::find(cmd.aliases.begin(), cmd.aliases.end(), inputCommand) != cmd.aliases.end();
        }
        if (!matches) {
            continue;
        }

        std::string extra;
        if (!cmd.allowArguments && (iss >> extra)) {
            return false;
        }

        if (!hasCommandPermission(cmd.requiredUserType, currentUser.type)) {
            colorcout("red", "Access Denied.\n");
        } else if (cmd.name == "help") {
            colorcout("cyan", "Available commands:\n");
            for (const auto& helpCmd : commands) {
                if (helpCmd.hidden ||
                    !hasCommandPermission(helpCmd.requiredUserType, currentUser.type)) {
                    continue;
                }
                colorcout("white", " - " + helpCmd.usage + ": " + helpCmd.description + "\n");
            }
        } else if (cmd.name == "dbg:help") {
            colorcout("cyan", "Available debug commands:\n");
            for (const auto& helpCmd : commands) {
                if (helpCmd.name.rfind("dbg:", 0) != 0 ||
                    !hasCommandPermission(helpCmd.requiredUserType, currentUser.type)) {
                    continue;
                }
                colorcout("white", " - " + helpCmd.usage + ": " + helpCmd.description + "\n");
            }
        } else {
            cmd.handler(input);
        }
        return true;
    }

    return false;
}

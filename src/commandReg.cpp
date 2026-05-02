#include "commandReg.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "color.hpp"

namespace {
std::string toLowerCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::vector<const RegisteredCommand*> sortedCommandsForHelp(
    const std::vector<RegisteredCommand>& commands
) {
    std::vector<const RegisteredCommand*> sortedCommands;
    sortedCommands.reserve(commands.size());
    for (const auto& command : commands) {
        sortedCommands.push_back(&command);
    }

    std::stable_sort(
        sortedCommands.begin(),
        sortedCommands.end(),
        [](const RegisteredCommand* lhs, const RegisteredCommand* rhs) {
            const std::string lhsName = toLowerCopy(lhs->name);
            const std::string rhsName = toLowerCopy(rhs->name);
            if (lhsName != rhsName) {
                return lhsName < rhsName;
            }
            return lhs->name < rhs->name;
        }
    );

    return sortedCommands;
}
}

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
            const auto sortedCommands = sortedCommandsForHelp(commands);
            for (const auto* helpCmd : sortedCommands) {
                if (helpCmd->hidden ||
                    !hasCommandPermission(helpCmd->requiredUserType, currentUser.type)) {
                    continue;
                }
                colorcout("white", " - " + helpCmd->usage + ": " + helpCmd->description + "\n");
            }
        } else if (cmd.name == "dbg:help") {
            colorcout("cyan", "Available debug commands:\n");
            const auto sortedCommands = sortedCommandsForHelp(commands);
            for (const auto* helpCmd : sortedCommands) {
                if (helpCmd->name.rfind("dbg:", 0) != 0 ||
                    !hasCommandPermission(helpCmd->requiredUserType, currentUser.type)) {
                    continue;
                }
                colorcout("white", " - " + helpCmd->usage + ": " + helpCmd->description + "\n");
            }
        } else {
            cmd.handler(input);
        }
        return true;
    }

    return false;
}

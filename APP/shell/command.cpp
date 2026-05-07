//Attention: Windows only code.
#include "command.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "color.hpp"
#include "commandHandlers.hpp"
#include "commandReg.hpp"
#include "udata.hpp"

#ifndef TUNDRAUX_DEFAULT_USER_TYPE                    //This default type is set in cmakelists.txt.
#define TUNDRAUX_DEFAULT_USER_TYPE "guest"
#endif

#ifndef TUNDRAUX_DEFAULT_USER_NAME
#define TUNDRAUX_DEFAULT_USER_NAME ""
#endif

std::string guessSimilarCommand(
    const std::string& input,
    const std::vector<RegisteredCommand>& commands,
    const USER& currentUser
);
bool isLikelyCmd(const std::string& input);

bool canRunSystemCommand(const USER& currentUser) {
    return currentUser.type == "admin" || currentUser.type == "debug";
}

bool redrawsShellHeader(const std::string& input) {
    std::istringstream iss(input);
    std::string command;
    std::string extra;
    iss >> command;
    return (command == "cls" || command == "clear") && !(iss >> extra);
}

void task_main() {
    renderShellHeader();

    USER currentUser = {
        TUNDRAUX_DEFAULT_USER_TYPE,
        TUNDRAUX_DEFAULT_USER_NAME,
        "",
        "",
        0
    };

    std::vector<RegisteredCommand> registeredCommands = buildNewCommandRegistry(currentUser);
    std::vector<std::string> commandHistory;
    int historyIndex = -1;
    const int MAX_HISTORY = 100;
    bool shellHeaderWasJustRendered = true;

    while (true) {
        if (shellHeaderWasJustRendered) {
            shellHeaderWasJustRendered = false;
        } else {
            colorcout("", "\n");
        }
        if (currentUser.type == "debug") {
            set_title("TundraUX 2.0 [DEBUG MODE]");
            colorcout("magenta", "DEBUG MODE ACTIVE>> ");
        } else if (currentUser.name.empty()) {
            set_title("TundraUX 2.0 [GUEST]");
            colorcout("magenta", "GUEST>> ");
        } else {
            set_title("TundraUX 2.0 [" + currentUser.type + "] " + currentUser.name);
            colorcout("magenta", currentUser.name + ">> ");
        }

        std::string input = readLineWithHistory(commandHistory, historyIndex);
        if (input.empty()) {
            continue;
        }

        if (commandHistory.empty() || commandHistory.back() != input) {
            if (static_cast<int>(commandHistory.size()) >= MAX_HISTORY) {
                commandHistory.erase(commandHistory.begin());
            }
            commandHistory.push_back(input);
        }
        historyIndex = -1;

        if (input.length() > 1 && input[0] == '/') {
            if (!canRunSystemCommand(currentUser)) {
                colorcout("red", "Access Denied.\n");
                continue;
            }
            std::string command = input.substr(1);
            colorcout("yellow", "=== Executing: " + command + " ===\n");
            int result = system(command.c_str());
            colorcout(result == 0 ? "green" : "red",
                     result == 0 ? "Command executed successfully.\n" : "Command failed.\n");
            colorcout("green", "=== Execution Complete ===\n\n");
            continue;
        }

        if (input == "/") {
            colorcout("red", "Error: Usage: /<command>\n\n");
            continue;
        }

        const bool commandRedrawsShellHeader = redrawsShellHeader(input);
        if (tryExecuteRegisteredCommand(input, registeredCommands, currentUser)) {
            shellHeaderWasJustRendered = commandRedrawsShellHeader;
            continue;
        }

        colorcout("yellow", "Unknown command: " + input + "\n");
        std::string suggestion = guessSimilarCommand(input, registeredCommands, currentUser);
        if (canRunSystemCommand(currentUser) && isLikelyCmd(input)) {
            colorcout("yellow", "Hint: Use \"/\" prefix for CMD commands\n");
        } else if (!suggestion.empty()) {
            colorcout("yellow", "Did you mean: " + suggestion + "?\n");
        }
    }
}

int boundedLevenshtein(const std::string& a, const std::string& b, int maxDist) {
    if (a == b) return 0;
    int na = static_cast<int>(a.size());
    int nb = static_cast<int>(b.size());
    if (std::abs(na - nb) > maxDist) return maxDist + 1;
    if (na == 0 || nb == 0) return (std::max(na, nb) <= maxDist) ? std::max(na, nb) : (maxDist + 1);

    std::vector<int> prev(nb + 1), cur(nb + 1);
    for (int j = 0; j <= nb; ++j) prev[j] = j;

    for (int i = 1; i <= na; ++i) {
        cur[0] = i;
        int rowMin = cur[0];
        for (int j = 1; j <= nb; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
            rowMin = std::min(rowMin, cur[j]);
        }
        if (rowMin > maxDist) return maxDist + 1;
        std::swap(prev, cur);
    }
    return prev[nb];
}

std::string guessSimilarCommand(
    const std::string& input,
    const std::vector<RegisteredCommand>& commands,
    const USER& currentUser
) {
    std::istringstream iss(input);
    std::string token;
    iss >> token;
    if (token.empty()) return "";

    int bestDist = 3;
    std::string bestMatch;

    auto considerCandidate = [&](const std::string& candidate) {
        int dist = boundedLevenshtein(token, candidate, bestDist - 1);
        if (dist < bestDist) {
            bestDist = dist;
            bestMatch = candidate;
        }
    };

    for (const auto& cmd : commands) {
        if (cmd.hidden || !hasCommandPermission(cmd.requiredUserType, currentUser.type)) {
            continue;
        }
        considerCandidate(cmd.name);
        for (const auto& alias : cmd.aliases) {
            considerCandidate(alias);
        }
        if (bestDist == 1) {
            break;
        }
    }
    return (bestDist >= 1 && bestDist <= 2) ? bestMatch : "";
}

bool isLikelyCmd(const std::string& input) {
    std::istringstream iss(input);
    std::string token;
    iss >> token;
    if (token.empty()) return false;
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });

    static const std::unordered_set<std::string> winCmds = {
        "dir", "cd", "echo", "del", "erase", "copy", "xcopy", "robocopy", "move",
        "ren", "rename", "type", "more", "find", "findstr", "fc", "attrib", "color", "title",
        "set", "path", "ver", "tasklist", "taskkill", "start", "shutdown", "sfc", "chkdsk",
        "ipconfig", "ping", "tracert", "net", "sc", "whoami", "where", "for", "call", "pause",
        "help", "reg", "wmic", "systeminfo", "hostname", "date", "time", "md", "mkdir", "rd", "rmdir"
    };
    return winCmds.count(lower) > 0;
}

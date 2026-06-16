//Attention: Windows only code.
#include "command.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <unordered_set>
#include <vector>

#include <TundraTUI/input.hpp>

#include "audit_log.hpp"
#include "backend_facade.hpp"
#include "backend_runtime.hpp"
#include "color.hpp"
#include "commandHandlers.hpp"
#include "commandReg.hpp"
#include "command_key_audit.hpp"

#ifndef TUNDRAUX_DEFAULT_USER_TYPE                    //This default type is set in cmakelists.txt.
#define TUNDRAUX_DEFAULT_USER_TYPE "guest"
#endif

#ifndef TUNDRAUX_DEFAULT_USER_NAME
#define TUNDRAUX_DEFAULT_USER_NAME ""
#endif

std::string guessSimilarCommand(
    const std::string& input,
    const std::vector<RegisteredCommand>& commands,
    const tundraux::frontend::ShellUser& currentUser
);
bool isLikelyCmd(const std::string& input);

bool canRunSystemCommand(const tundraux::frontend::ShellUser& currentUser) {
    return currentUser.type == "admin" || currentUser.type == "debug";
}

namespace {
class LocalAuditSink : public tundraux::frontend::FrontendAuditSink {
public:
    LocalAuditSink() {
        tundraux::audit::initialize();
    }

    void setCurrentUser(const tundraux::frontend::ShellUser& user) override {
        currentUser_ = user;
        tundraux::audit::setCurrentUser(USER{
            currentUser_.type,
            currentUser_.name,
            "",
            currentUser_.passwordHint,
            currentUser_.failedCount
        });
    }

    tundraux::frontend::FacadeResult logEvent(
        const std::string& category,
        const std::string& detail
    ) override {
        tundraux::audit::logEvent(category, detail);
        return {true, "", ""};
    }

    tundraux::frontend::FacadeResult logKeyPress(
        const std::string& key,
        bool sensitive
    ) override {
        tundraux::audit::logKeyPress(
            tundraux::frontend::keyPressFromFrontendAuditText(key),
            sensitive
        );
        return {true, "", ""};
    }

private:
    tundraux::frontend::ShellUser currentUser_;
};

tundraux::frontend::FrontendAuditSink* g_commandKeyAuditSink = nullptr;

void dispatchCommandKeyAudit(
    const tundra_tui::KeyPress& key,
    bool sensitive
) {
    if (g_commandKeyAuditSink == nullptr) {
        return;
    }
    const std::string keyText = tundraux::frontend::toFrontendAuditKeyText(key);
    g_commandKeyAuditSink->logKeyPress(keyText, sensitive);
}

void syncStrictModeFromBackendFacade(tundraux::frontend::BackendFacade& facade) {
    const auto result = facade.getStrictMode();
    if (result.ok) {
        tundraux::audit::setStrictModeEnabled(result.value);
    }
}
} // namespace

bool usesBackendRuntime(tundraux::frontend::BackendRuntime* backendRuntime) {
    return backendRuntime != nullptr && backendRuntime->client() != nullptr;
}

tundraux::frontend::ShellUser guestShellUser() {
    return {
        "guest",
        "",
        "",
        0
    };
}

void setAuditCurrentUser(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const tundraux::frontend::ShellUser& currentUser
) {
    if (auditSink == nullptr) {
        return;
    }
    auditSink->setCurrentUser(currentUser);
}

void logAuditEvent(
    tundraux::frontend::FrontendAuditSink* auditSink,
    const tundraux::frontend::ShellUser& currentUser,
    const std::string& category,
    const std::string& detail
) {
    if (auditSink == nullptr) {
        return;
    }
    setAuditCurrentUser(auditSink, currentUser);
    auditSink->logEvent(category, detail);
}

bool syncSystemCommandUserFromBackend(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime& backendRuntime,
    tundraux::frontend::BackendFacade* facade,
    tundraux::frontend::FrontendAuditSink* auditSink,
    std::string& denyMessage
) {
    (void)backendRuntime;
    if (facade == nullptr) {
        currentUser = guestShellUser();
        setAuditCurrentUser(auditSink, currentUser);
        denyMessage = "Backend unavailable.";
        return false;
    }

    const auto profileResult = facade->refreshProfile();
    if (profileResult.ok) {
        currentUser = profileResult.value;
        setAuditCurrentUser(auditSink, currentUser);
        denyMessage = "Access Denied.";
        return canRunSystemCommand(currentUser);
    }

    currentUser = guestShellUser();
    setAuditCurrentUser(auditSink, currentUser);
    if (profileResult.errorCode == "SessionExpired") {
        denyMessage = "Backend session expired.";
    } else if (profileResult.errorCode == "TransportError") {
        denyMessage = "Backend unavailable.";
    } else if (profileResult.errorCode == "PermissionDenied") {
        denyMessage = "Access Denied.";
    } else {
        denyMessage = profileResult.message.empty() ? "Unable to verify backend identity." : profileResult.message;
    }
    return false;
}

bool redrawsShellHeader(const std::string& input) {
    std::istringstream iss(input);
    std::string command;
    std::string extra;
    iss >> command;
    return (command == "cls" || command == "clear") && !(iss >> extra);
}

void task_main(tundraux::frontend::BackendRuntime* backendRuntime) {
    std::unique_ptr<tundraux::frontend::BackendFacade> backendFacade;
    std::unique_ptr<tundraux::frontend::BackendAuditSink> backendAuditSink;
    std::unique_ptr<LocalAuditSink> localAuditSink;
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr;
    if (backendRuntime != nullptr) {
        backendFacade = std::make_unique<tundraux::frontend::BackendFacade>(*backendRuntime);
        if (backendFacade->active()) {
            syncStrictModeFromBackendFacade(*backendFacade);
            backendAuditSink = std::make_unique<tundraux::frontend::BackendAuditSink>(*backendFacade);
            auditSink = backendAuditSink.get();
        } else {
            localAuditSink = std::make_unique<LocalAuditSink>();
            auditSink = localAuditSink.get();
        }
    } else {
        localAuditSink = std::make_unique<LocalAuditSink>();
        auditSink = localAuditSink.get();
    }

    renderShellHeader();
    if (auditSink != nullptr) {
        g_commandKeyAuditSink = auditSink;
        tundra_tui::setKeyAuditSink([](const tundra_tui::KeyPress& key, bool sensitive) {
            dispatchCommandKeyAudit(key, sensitive);
        });
    } else {
        g_commandKeyAuditSink = nullptr;
        tundra_tui::setKeyAuditSink(nullptr);
    }

    tundraux::frontend::ShellUser currentUser = {
        TUNDRAUX_DEFAULT_USER_TYPE,
        TUNDRAUX_DEFAULT_USER_NAME,
        "",
        0
    };
    setAuditCurrentUser(auditSink, currentUser);

    std::vector<RegisteredCommand> registeredCommands = buildNewCommandRegistry(currentUser, backendRuntime, auditSink);
    std::vector<std::string> commandHistory;
    int historyIndex = -1;
    const int MAX_HISTORY = 100;
    bool shellHeaderWasJustRendered = true;

    while (true) {
        setAuditCurrentUser(auditSink, currentUser);
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
        logAuditEvent(auditSink, currentUser, "shell", "input " + input);

        if (commandHistory.empty() || commandHistory.back() != input) {
            if (static_cast<int>(commandHistory.size()) >= MAX_HISTORY) {
                commandHistory.erase(commandHistory.begin());
            }
            commandHistory.push_back(input);
        }
        historyIndex = -1;

        if (input.length() > 1 && input[0] == '/') {
            if (usesBackendRuntime(backendRuntime)) {
                std::string denyMessage;
                if (!syncSystemCommandUserFromBackend(
                        currentUser,
                        *backendRuntime,
                        backendFacade.get(),
                        auditSink,
                        denyMessage)) {
                    logAuditEvent(auditSink, currentUser, "shell", "system denied backend");
                    colorcout("red", denyMessage + "\n");
                    continue;
                }
            }

            if (!canRunSystemCommand(currentUser)) {
                logAuditEvent(auditSink, currentUser, "shell", "system denied");
                colorcout("red", "Access Denied.\n");
                continue;
            }
            logAuditEvent(auditSink, currentUser, "shell", "system execute");
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
        if (tryExecuteRegisteredCommand(input, registeredCommands, currentUser, auditSink)) {
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
    const tundraux::frontend::ShellUser& currentUser
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

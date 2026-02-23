#include "command.h"
#include <string>
#include <iostream>
#include "color.h"
#include "udata.h"
#include <vector>
#include <conio.h>
#include <functional>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include "crypto.h"
#include "manageusers.h"
#include "hello.h"
#include "debug.h"
#include "TUXfile.h"
#include <chrono>   
#include <iomanip>  

#define DEFAULT_USER_TYPE "debug" //set to "guest" for production
#define DEFAULT_USER_NAME "debug" //set to "" for production

std::string guessSimilarCommand(const std::string& input);
bool isLikelyCmd(const std::string& input);

void task_main() {
    clear_screen();
    print_icon();
    std::cout << std::endl;
    USER currentUser = {
        DEFAULT_USER_TYPE,//type
        DEFAULT_USER_NAME,//name
        "",//password
        "",//password_hint
        0//count
    };
    using CommandHandler = std::function<void(const std::string&)>;
    struct CommandEntry {
        std::string prefix;
        CommandHandler handler;
        bool exactMatch;
        std::string requiredUserType;
    };
    std::vector<std::string> commandHistory;
    int historyIndex = -1;
    const int MAX_HISTORY = 100;

    std::vector<CommandEntry> commands = {
        // Define commands here:
        // IMPORTANT: For commands that take arguments, set exactMatch to false
        // so that the prefix match can work correctly.
        // Format: {prefix, handler, exactMatch, requiredUserType}
        // Also remember to update help command when adding new commands.
        {"exit", [](const std::string &)
         {
            //  if (getYN("Make sure to save all changes before exit. Proceed to exit?"))
            //  {
            //      exit(0);
            //  }
            exit(0);
         },
         true, ""},
        {"help", [](const std::string &)
         {
             colorcout("cyan", "Available commands:\n");
             colorcout("white", " - exit: Exit the program\n");
             colorcout("white", " - cls: Clear the screen\n");
             colorcout("white", " - login <username>: Log in as specified user\n");
             colorcout("white", " - logout: Log out current user\n");
             colorcout("white", " - listuser: List all users\n");
             colorcout("white", " - manageuser: Open user management interface\n");
             colorcout("white", " - modify: Modify current user information\n");
             colorcout("white", " - importdata: Import user data from old versions\n");
             colorcout("white", " - TUXfile: Open TUX File Manager\n");
             colorcout("white", " - time: Display current system time and timestamp\n");
             colorcout("white", " - license: Show terms of use license\n");
             colorcout("white", " - displaytest: Run display test\n");
             colorcout("white", " - info: Show program information\n");
             colorcout("white", " - help: Show this help message\n");
         },
         true, ""},
        {"importdata", [](const std::string &)
         {
             ReadOldFile();
         },
         true, "admin,debug"},
         {"time", [](const std::string &){ 
            auto now = std::chrono::system_clock::now();
            std::time_t tt = std::chrono::system_clock::to_time_t(now);
            auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            std::tm local_tm{};
            localtime_s(&local_tm, &tt);
            std::ostringstream oss;
            oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
            colorcout("white", "Current time: " + oss.str() + "\n");
            colorcout("white", "Timestamp: " + std::to_string(ts) + "\n");
         }, true, ""},
        {"modify", [&currentUser](const std::string &)
         {
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
         },
         true, ""},
        {"cls", [](const std::string &)
         {
             clear_screen();
             print_icon();
             std::cout << std::endl;
         },
         true, ""},
        {"login ", [&currentUser](const std::string &input)
         {
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
         },
         false, "guest,debug"},
        {"logout", [&currentUser](const std::string &)
         {
             if (currentUser.name.empty())
             {
                 colorcout("yellow", "No user is currently logged in.\n");
                 return;
             }
             colorcout("green", "User " + currentUser.name + " logged out successfully.\n");
             currentUser = {
                 DEFAULT_USER_TYPE, // type
                 DEFAULT_USER_NAME, // name
                 "",                // password
                 "",                // password_hint
                 0                  // count
             };
         },
         true, ""},
        {"listuser", [](const std::string &){ listUser(); }, true, ""},
        {"TUXfile", [&currentUser](const std::string &){
            file_editor(currentUser.name, currentUser.type);
        }, true, "user,admin,debug"},
        {"info", [](const std::string &){
            constexpr const char* BUILD = "Build: " __TIMESTAMP__;
            colorcout("cyan", "TundraUX 2.0 " + std::string(BUILD) + "\n");
        }, true, ""},
        {"license", [](const std::string &){ license(); }, true, ""},
        {"manageuser", [](const std::string &){ manage_users(); }, true, "admin,debug"},
        {"createfile", [](const std::string &){ createfile(); }, true, "debug"},
        {"hello", [](const std::string &){ hello(); }, true, "debug"},
        {"deletefile", [](const std::string &){ delete_file(); }, true, "debug"},
        {"structfile", [](const std::string &){ struct_file();}, true, "debug"},
        {"displaytest", [](const std::string &){ display_test(); }, true, ""}
    };

    while (true) {
        std::cout << std::endl;
        if(currentUser.type == "debug") {
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
        if (!input.empty()) {
            if (commandHistory.empty() || commandHistory.back() != input) {
                if (static_cast<int>(commandHistory.size()) >= MAX_HISTORY) {
                    commandHistory.erase(commandHistory.begin());
                }
                commandHistory.push_back(input);
            }
            historyIndex = -1;

            if (input.length() > 1 && input[0] == '/') {
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
            // Find and execute command
            bool commandFound = false;
            for (const auto& cmd : commands) {
                if ((cmd.exactMatch && input == cmd.prefix) ||
                    (!cmd.exactMatch && input.rfind(cmd.prefix, 0) == 0)) {
                    if (!cmd.requiredUserType.empty()) {
                        // Split requiredUserType by comma and check if currentUser.type matches any
                        std::istringstream ss(cmd.requiredUserType);
                        std::string userType;
                        bool hasPermission = false;
                        while (std::getline(ss, userType, ',')) {
                            // Trim whitespace
                            userType.erase(0, userType.find_first_not_of(" \t"));
                            userType.erase(userType.find_last_not_of(" \t") + 1);
                            if (userType == currentUser.type) {
                                hasPermission = true;
                                break;
                            }
                        }
                        if (!hasPermission) {
                            colorcout("red", "Access Denied.\n");
                            commandFound = true;
                            break;
                        }
                    }
                    cmd.handler(input);
                    commandFound = true;
                    break;
                }
            }
            if (!commandFound) {
                colorcout("yellow", "Unknown command: " + input + "\n");
                std::string suggestion = guessSimilarCommand(input);
                if (isLikelyCmd(input)) {
                    colorcout("yellow", "Hint: Use \"/\" prefix for CMD commands\n");
                } else if (!suggestion.empty()) {
                    colorcout("yellow", "Did you mean: " + suggestion + "?\n");
                }
            }
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

std::string guessSimilarCommand(const std::string& input) {
    std::istringstream iss(input);
    std::string token;
    iss >> token;
    if (token.empty()) return "";
    static const std::vector<std::string> known = {
        "exit","help","cls","login","logout","manageuser","modify","TUXfile",
        "license","listuser","time","importdata","info","displaytest"
    };
    int bestDist = 3;  // Max distance to consider
    std::string bestMatch;
    for (const auto& cmd : known) {
        int dist = boundedLevenshtein(token, cmd, bestDist - 1);
        if (dist < bestDist) {
            bestDist = dist;
            bestMatch = cmd;
            if (bestDist == 1) break; // can't get better than 1
        }
    }
    return (bestDist >= 1 && bestDist <=2) ? bestMatch : "";
}

bool isLikelyCmd(const std::string& input) {
    std::istringstream iss(input);
    std::string token;
    iss >> token;
    if (token.empty()) return false;
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });

    static const std::unordered_set<std::string> winCmds = {
        "dir","cd","echo","del","erase","copy","xcopy","robocopy","move",
        "ren","rename","type","more","find","findstr","fc","attrib","color","title",
        "set","path","ver","tasklist","taskkill","start","shutdown","sfc","chkdsk",
        "ipconfig","ping","tracert","net","sc","whoami","where","for","call","pause",
        "help","reg","wmic","systeminfo","hostname","date","time","md","mkdir","rd","rmdir"
    };
    return winCmds.count(lower) > 0;
}
//Attention: windows only code.
#include "command.h"
#include "manageusers.h"
#include "color.h"
#include "udata.h"
#include <string>
#include <iostream>
#include <vector>
#include "crypto.h"
#include <fstream>
#include <algorithm>
#include <sstream>
#include <map>
#include <conio.h>

// Helper function to display user details
void displayUserDetails(const USER& user) {
    colorcout("cyan", "\n=== User Details ===\n");
    colorcout("white", "Name: " + user.name + "\n");
    colorcout("white", "Type: " + user.type + "\n");
    colorcout("white", "Password: " + user.password + "\n");
    colorcout("white", "Password Hint: " + (user.password_hint.empty() ? "(none)" : user.password_hint) + "\n");
    colorcout("white", "Count: " + std::to_string(user.count) + "\n");
}

static void printUserHelp() {
    colorcout("cyan", "=== Available commands ===\n");
    colorcout("white", "  help (h)                 show this help message\n");
    colorcout("white", "  list (ls, l)             list all users\n");
    colorcout("white", "  show (s) <username>      show user details\n");
    colorcout("white", "  add (a) <username> type=<t> password=<p> [hint=<h>] [count=<n>]\n");
    colorcout("white", "  set (u) <username> field=value...   update specified fields (type/password/hint/count)\n");
    colorcout("white", "  delete (del, rm, d) <username>      delete user\n");
    colorcout("white", "  exit (q)                 exit\n");
}

static std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

static std::map<std::string, std::string> parseOptions(const std::vector<std::string>& tokens, size_t startIdx) {
    std::map<std::string, std::string> opts;
    for (size_t i = startIdx; i < tokens.size(); ++i) {
        auto pos = tokens[i].find('=');
        if (pos != std::string::npos && pos > 0 && pos + 1 < tokens[i].size()) {
            opts[tokens[i].substr(0, pos)] = tokens[i].substr(pos + 1);
        }
    }
    return opts;
}

void manage_users() {
    colorcout("cyan", "User management application v1.0\n");
    set_title("User Management");
    std::ifstream check("user_data.dat");
    if (!check.good()) {
        colorcout("red", "Error: user_data.dat not found.\n");
        return;
    }
    check.close();
    DataManager dataManager("user_data.dat");
    printUserHelp();
    std::vector<std::string> commandHistory;
    int historyIndex = -1;
    const int MAX_HISTORY = 100;

    while (true) {
        colorcout("white", "\n > ");
        std::string line = readLineWithHistory(commandHistory, historyIndex);
        if (line.empty()) continue;
        if (commandHistory.empty() || commandHistory.back() != line) {
            if (static_cast<int>(commandHistory.size()) >= MAX_HISTORY) {
                commandHistory.erase(commandHistory.begin());
            }
            commandHistory.push_back(line);
        }
        historyIndex = -1;
        auto tokens = splitTokens(line);
        if (tokens.empty()) continue;
        const auto& cmd = tokens[0];

        if (cmd == "exit" || cmd == "quit" || cmd == "q") {
            break;
        } 
        else if (cmd == "help" || cmd == "h" || cmd == "?") {
            printUserHelp();
        } 
        else if (cmd == "list" || cmd == "ls" || cmd == "l") {
            auto usernames = dataManager.GetAllUsernames();
            if (usernames.empty()) {
                colorcout("yellow", "No users found.\n");
            } else {
                for (const auto& name : usernames) {
                    colorcout("white", "  - " + name + "\n");
                }
            }
        } 
        else if ((cmd == "show" || cmd == "s") && tokens.size() >= 2) {
            auto users = dataManager.GetAllUsers();
            auto it = std::find_if(users.begin(), users.end(), [&](const USER& u){ return u.name == tokens[1]; });
            
            if (it != users.end()) {
                displayUserDetails(*it);
            } else {
                colorcout("yellow", "User not found.\n");
            }
        } 
        else if ((cmd == "add" || cmd == "a") && tokens.size() >= 2) {
            USER newUser; 
            newUser.name = tokens[1];
            
            // Check if user already exists
            auto usernames = dataManager.GetAllUsernames();
            auto it = std::find(usernames.begin(), usernames.end(), newUser.name);
            if (it != usernames.end()) {
                colorcout("red", "Error: Username '" + newUser.name + "' already exists.\n");
                continue;
            }
            
            auto opts = parseOptions(tokens, 2);

            if (!opts.count("type") || !opts.count("password")) {
                colorcout("yellow", "Missing required parameters: type and password.\n");
                continue;
            }

            newUser.type = opts["type"];
            if (newUser.type == "debug" ) { 
                colorcout("red", "Access Denied.\n"); 
                continue; 
            }
            if (newUser.type!="admin" && newUser.type!="user") {
                colorcout("yellow", "Invalid user type. Allowed types: admin, user.\n");
                continue;
            }

            newUser.password = opts["password"];
            newUser.password_hint = opts.count("hint") ? opts["hint"] : "";
            newUser.count = opts.count("count") ? std::stoi(opts["count"]) : 0;

            if (dataManager.AddUser(newUser)) {
                colorcout("green", "User created successfully.\n");
            } else {
                colorcout("red", "Failed to create user.\n");
            }
        } 
        else if ((cmd == "set" || cmd == "u") && tokens.size() >= 2) {
            auto users = dataManager.GetAllUsers();
            auto it = std::find_if(users.begin(), users.end(), [&](const USER& u){ return u.name == tokens[1]; });
            
            if (it == users.end()) { 
                colorcout("yellow", "User not found.\n"); 
                continue; 
            }

            USER updated = *it;
            auto opts = parseOptions(tokens, 2);
            
            for (const auto& kv : opts) {
                if (kv.first == "type") {
                    if (kv.second == "debug") { 
                        colorcout("red", "Access Denied.\n"); 
                        continue; 
                    }
                    if (kv.second != "admin" && kv.second != "user") {
                        colorcout("yellow", "Invalid user type. Allowed types: admin, user.\n");
                        continue;
                    }
                    updated.type = kv.second;
                } else if (kv.first == "password") {
                    updated.password = kv.second;
                } else if (kv.first == "hint") {
                    updated.password_hint = kv.second;
                } else if (kv.first == "count") {
                    try { 
                        updated.count = std::stoi(kv.second); 
                    } catch (...) { 
                        colorcout("red", "Invalid count value.\n"); 
                    }
                }
            }

            if (dataManager.UpdateUser(tokens[1], updated)) {
                colorcout("green", "User updated successfully.\n");
            } else {
                colorcout("red", "Failed to update user.\n");
            }
        } 
        else if ((cmd == "delete" || cmd == "del" || cmd == "rm" || cmd == "d") && tokens.size() >= 2) {
            auto users = dataManager.GetAllUsers();
            auto it = std::find_if(users.begin(), users.end(), [&](const USER& u){ return u.name == tokens[1]; });
            
            if (it == users.end()) { 
                colorcout("yellow", "User not found.\n"); 
                continue; 
            }
            
            if (it->type == "admin") {
                colorcout("yellow", "Warning: deleting admin user.\n");
            }

            if (getYN("Are you sure you want to delete user '" + tokens[1] + "'?")) {
                if (dataManager.RemoveUser(tokens[1])) {
                    colorcout("green", "User deleted successfully.\n");
                } else {
                    colorcout("red", "Failed to delete user.\n");
                }
            }
        } 
        else {
            colorcout("yellow", "Unknown command, type help to see available commands.\n");
        }
    }

    colorcout("cyan", "Exiting application.\n");
}
#pragma once

#include <TundraTUI/input.hpp>

#include <filesystem>
#include <string>
#include <vector>

struct USER {
    std::string type;
    std::string name;
    std::string password;
    std::string password_hint;
    int count;
};

namespace tundraux::audit {

void initialize();
void refreshStrictMode();
void setStrictModeEnabled(bool enabled);
void setCurrentUser(const USER& user);
bool isStrictModeEnabled();
std::filesystem::path startupLogPath();
void logEvent(const std::string& category, const std::string& detail);
void logKeyPress(const tundra_tui::KeyPress& key, bool sensitive);
bool isPrivileged(const std::string& usertype);
int openTlogInEditor(
    const std::string& path,
    const std::string& displayName,
    const std::string& username,
    const std::string& usertype
);
bool exportTlogToPlaintext(
    const std::string& inputPath,
    const std::string& username,
    const std::string& usertype,
    std::string& message
);
std::vector<std::string> readTlogPlaintext(const std::filesystem::path& path, std::string& error);

} // namespace tundraux::audit

#include "legacy_direct.hpp"

namespace tundraux::legacy_direct {
namespace {

bool unavailable(std::string& message) {
    message = "Legacy direct mode is unavailable in this build.";
    return false;
}

} // namespace

bool login(const std::string&, const std::string&, LoginResult& result) {
    return unavailable(result.message);
}

bool listUsers(std::vector<ShellUser>&, std::string& message) {
    return unavailable(message);
}

bool getStrictMode(bool&, std::string& message) {
    return unavailable(message);
}

bool setStrictMode(bool, std::string& message) {
    return unavailable(message);
}

bool loadAccount(const ShellUser&, AccountRecord&, std::string& message) {
    return unavailable(message);
}

bool saveAccount(
    const std::string&,
    bool,
    const std::string&,
    const std::string&,
    AccountRecord&,
    std::string& message
) {
    return unavailable(message);
}

bool debugCreateFile(std::string& message) {
    return unavailable(message);
}

bool debugDeleteFile(std::string& message) {
    return unavailable(message);
}

bool debugForceLogin(const std::string&, ShellUser&, std::string& message) {
    return unavailable(message);
}

bool createInitialAdmin(const std::string&, const std::string&, const std::string&, std::string& message) {
    return unavailable(message);
}

} // namespace tundraux::legacy_direct

#include "audit_log.hpp"
#include "crypto.hpp"

#include <TundraTUI/input.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int run_editor(const std::string&, const std::string&) {
    return 0;
}

namespace {

void writeString(std::ofstream& out, const std::string& value) {
    const std::size_t length = value.size();
    out.write(reinterpret_cast<const char*>(&length), sizeof(length));
    out.write(value.data(), static_cast<std::streamsize>(length));
}

bool writeStrictUserData() {
    std::ofstream out("user_data.dat", std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    const int version = 21;
    const std::uint8_t strictMode = 1;
    const std::size_t userCount = 1;
    const int failedCount = 0;

    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&strictMode), sizeof(strictMode));
    out.write(reinterpret_cast<const char*>(&userCount), sizeof(userCount));
    writeString(out, "admin");
    writeString(out, "tester");
    writeString(out, encrypt("password"));
    writeString(out, "hint");
    out.write(reinterpret_cast<const char*>(&failedCount), sizeof(failedCount));
    return static_cast<bool>(out);
}

bool containsLineFragment(const std::vector<std::string>& lines, const std::string& fragment) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(fragment) != std::string::npos;
    });
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    const fs::path originalCwd = fs::current_path();
    const fs::path workspace = originalCwd / "audit_log_test_workspace";
    std::error_code error;
    fs::remove_all(workspace, error);
    fs::create_directories(workspace, error);
    if (error) {
        std::cerr << "failed to create test workspace: " << error.message() << "\n";
        return 1;
    }

    fs::current_path(workspace, error);
    if (error) {
        std::cerr << "failed to enter test workspace: " << error.message() << "\n";
        return 1;
    }

    if (!writeStrictUserData()) {
        std::cerr << "failed to write strict user data\n";
        return 1;
    }

    tundraux::audit::initialize();
    tundraux::audit::setCurrentUser(USER{"admin", "tester", "", "", 0});
    tundraux::audit::logKeyPress({tundra_tui::Key::Character, 's'}, true);
    tundraux::audit::logKeyPress({tundra_tui::Key::Enter, '\0'}, true);
    tundraux::audit::logKeyPress({tundra_tui::Key::Character, 'x'}, false);

    std::string readError;
    const std::vector<std::string> lines =
        tundraux::audit::readTlogPlaintext(tundraux::audit::startupLogPath(), readError);
    if (!readError.empty()) {
        std::cerr << "failed to read audit log: " << readError << "\n";
        return 1;
    }

    if (containsLineFragment(lines, "Character 's'")) {
        std::cerr << "sensitive character was written to audit log\n";
        return 1;
    }
    if (!containsLineFragment(lines, "Character [redacted]")) {
        std::cerr << "sensitive character was not redacted\n";
        return 1;
    }
    if (!containsLineFragment(lines, "Enter")) {
        std::cerr << "sensitive control key was not logged\n";
        return 1;
    }
    if (!containsLineFragment(lines, "Character 'x'")) {
        std::cerr << "non-sensitive character detail was not logged\n";
        return 1;
    }

    fs::current_path(originalCwd, error);
    fs::remove_all(workspace, error);
    return 0;
}

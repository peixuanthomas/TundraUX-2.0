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

namespace {

std::vector<std::string> g_editorViewLines;

std::vector<std::string> readTextLines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

bool containsLineFragment(const std::vector<std::string>& lines, const std::string& fragment) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(fragment) != std::string::npos;
    });
}

std::string lineContaining(const std::vector<std::string>& lines, const std::string& fragment) {
    const auto found = std::find_if(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(fragment) != std::string::npos;
    });
    return found == lines.end() ? std::string{} : *found;
}

} // namespace

int run_editor(const std::string& filepath, const std::string&) {
    g_editorViewLines = readTextLines(filepath);
    return 0;
}

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

    tundraux::audit::setStrictModeEnabled(true);
    const fs::path lazyInitializedLogPath = tundraux::audit::startupLogPath();
    if (!tundraux::audit::isStrictModeEnabled()) {
        std::cerr << "lazy audit initialization cleared strict mode\n";
        return 1;
    }
    tundraux::audit::setCurrentUser(USER{"admin", "tester", "", "", 0});
    tundraux::audit::logEvent("strict", "lazy initialization preserved strict mode");

    std::string lazyReadError;
    const std::vector<std::string> lazyLines =
        tundraux::audit::readTlogPlaintext(lazyInitializedLogPath, lazyReadError);
    if (!lazyReadError.empty()) {
        std::cerr << "failed to read lazily initialized audit log: " << lazyReadError << "\n";
        return 1;
    }
    if (!containsLineFragment(lazyLines, "lazy initialization preserved strict mode")) {
        std::cerr << "strict-mode audit event missing after lazy initialization\n";
        return 1;
    }

    tundraux::audit::setStrictModeEnabled(true);
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

    tundraux::audit::logEvent("explorer", "open Logs/audit.tlog");
    tundraux::audit::logEvent("key", "Character 'z'");

    g_editorViewLines.clear();
    const int openResult = tundraux::audit::openTlogInEditor(
        tundraux::audit::startupLogPath().string(),
        "audit-test.tlog",
        "tester",
        "admin"
    );
    if (openResult != 0) {
        std::cerr << "failed to open audit log in editor: " << openResult << "\n";
        return 1;
    }

    const std::string explorerLine = lineContaining(g_editorViewLines, "explorer");
    const std::string keyLine = lineContaining(g_editorViewLines, "Character 'z'");
    if (explorerLine.empty() || keyLine.empty()) {
        std::cerr << "editor view did not include expected audit records\n";
        return 1;
    }

    const std::size_t explorerCategory = explorerLine.find("explorer");
    const std::size_t keyCategory = keyLine.find("key");
    const std::size_t explorerDetail = explorerLine.find("open Logs/audit.tlog");
    const std::size_t keyDetail = keyLine.find("Character 'z'");
    if (explorerCategory == std::string::npos || keyCategory == std::string::npos ||
        explorerDetail == std::string::npos || keyDetail == std::string::npos) {
        std::cerr << "editor view did not preserve category or detail text\n";
        return 1;
    }
    if (explorerCategory != keyCategory || explorerDetail != keyDetail) {
        std::cerr << "editor view columns are not aligned\n";
        return 1;
    }

    fs::current_path(originalCwd, error);
    fs::remove_all(workspace, error);
    return 0;
}

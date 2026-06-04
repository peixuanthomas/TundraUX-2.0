#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SourceRule {
    const char* path;
    const char* forbidden;
};

std::string readFile(const std::filesystem::path& path, std::vector<std::string>& failures) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        failures.push_back("Unable to read " + path.string());
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

int lineNumberOf(const std::string& contents, std::size_t offset) {
    int line = 1;
    for (std::size_t index = 0; index < offset && index < contents.size(); ++index) {
        if (contents[index] == '\n') {
            ++line;
        }
    }
    return line;
}

void expectAbsent(
    const std::filesystem::path& root,
    const SourceRule& rule,
    std::vector<std::string>& failures) {
    const std::filesystem::path path = root / rule.path;
    const std::string contents = readFile(path, failures);
    if (contents.empty()) {
        return;
    }

    std::size_t offset = contents.find(rule.forbidden);
    while (offset != std::string::npos) {
        failures.push_back(
            std::string(rule.path) + ":" + std::to_string(lineNumberOf(contents, offset)) +
            " contains forbidden token `" + rule.forbidden + "`");
        offset = contents.find(rule.forbidden, offset + 1);
    }
}

bool isCMakeTokenBoundary(char value) {
    return value == ')' || value == '\r' || value == '\n' || value == '\t' || value == ' ';
}

std::string findCMakeCommandForTarget(
    const std::string& contents,
    const std::string& command,
    const std::string& target) {
    const std::string prefix = command + "(" + target;
    std::size_t searchFrom = 0;

    while (true) {
        const std::size_t start = contents.find(prefix, searchFrom);
        if (start == std::string::npos) {
            return {};
        }

        const std::size_t afterTarget = start + prefix.size();
        if (afterTarget >= contents.size() || !isCMakeTokenBoundary(contents[afterTarget])) {
            searchFrom = afterTarget;
            continue;
        }

        int depth = 0;
        for (std::size_t index = start + command.size(); index < contents.size(); ++index) {
            if (contents[index] == '(') {
                ++depth;
            } else if (contents[index] == ')') {
                --depth;
                if (depth == 0) {
                    return contents.substr(start, index - start + 1);
                }
            }
        }

        return contents.substr(start);
    }
}

void expectCMakeTargetBlockAbsent(
    const std::string& cmake,
    const std::string& command,
    const std::string& target,
    const std::string& forbidden,
    std::vector<std::string>& failures) {
    const std::string block = findCMakeCommandForTarget(cmake, command, target);
    if (block.empty()) {
        failures.push_back("CMakeLists.txt is missing " + command + " for " + target);
        return;
    }

    const std::size_t offset = block.find(forbidden);
    if (offset != std::string::npos) {
        failures.push_back(
            "CMakeLists.txt " + command + "(" + target + ") contains forbidden token `" +
            forbidden + "`");
    }
}

void expectPresent(
    const std::filesystem::path& root,
    const char* path,
    const char* required,
    std::vector<std::string>& failures) {
    const std::filesystem::path fullPath = root / path;
    const std::string contents = readFile(fullPath, failures);
    if (contents.empty()) {
        return;
    }

    if (contents.find(required) == std::string::npos) {
        failures.push_back(std::string(path) + " is missing required token `" + required + "`");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: frontend_backend_separation_tests <project-source-dir>\n";
        return 2;
    }

    const std::filesystem::path root = std::filesystem::u8path(argv[1]);
    std::vector<std::string> failures;

    const SourceRule sourceRules[] = {
        {"CORE/main/main.cpp", "#include \"legacy_direct.hpp\""},
        {"CORE/main/main.cpp", "tundraux::legacy_direct::"},
        {"CORE/startup/hello.cpp", "#include \"legacy_direct.hpp\""},
        {"CORE/startup/hello.cpp", "tundraux::legacy_direct::"},
        {"APP/shell/command.cpp", "#include \"legacy_direct.hpp\""},
        {"APP/shell/command.cpp", "tundraux::legacy_direct::"},
        {"APP/shell/commandHandlers.cpp", "#include \"legacy_direct.hpp\""},
        {"APP/shell/commandHandlers.cpp", "tundraux::legacy_direct::"},
        {"APP/account_settings/account_settings.cpp", "#include \"legacy_direct.hpp\""},
        {"APP/account_settings/account_settings.cpp", "tundraux::legacy_direct::"},
        {"SYSTEM/debug/debug.cpp", "#include \"legacy_direct.hpp\""},
        {"SYSTEM/debug/debug.cpp", "tundraux::legacy_direct::"},
        {"APP/explorer/explorer_open.cpp", "#include \"legacy_direct.hpp\""},
        {"APP/explorer/explorer_open.cpp", "tundraux::legacy_direct::"},
    };

    for (const SourceRule& rule : sourceRules) {
        expectAbsent(root, rule, failures);
    }

    const std::string cmake = readFile(root / "CMakeLists.txt", failures);
    if (!cmake.empty()) {
        expectCMakeTargetBlockAbsent(
            cmake,
            "target_link_libraries",
            "${PROJECT_NAME}",
            "tundraux_backend_core",
            failures);
        expectCMakeTargetBlockAbsent(
            cmake,
            "target_include_directories",
            "${PROJECT_NAME}",
            "${PROJECT_SOURCE_DIR}/BACKEND/core",
            failures);
        expectCMakeTargetBlockAbsent(
            cmake,
            "set",
            "TUNDRAUX_APP_COMMON_SOURCES",
            "APP/legacy_direct/legacy_direct.cpp",
            failures);
        expectCMakeTargetBlockAbsent(
            cmake,
            "set",
            "TUNDRAUX_APP_SOURCES",
            "APP/legacy_direct/legacy_direct.cpp",
            failures);
    }

    expectPresent(
        root,
        "APP/explorer/explorer_open.cpp",
        "tundraux::audit::openTlogInEditor",
        failures);
    expectPresent(root, "APP/explorer/explorer_open.cpp", "ShellExecuteW", failures);

    if (!failures.empty()) {
        std::cerr << "frontend/backend static separation violations:\n";
        for (const std::string& failure : failures) {
            std::cerr << " - " << failure << "\n";
        }
        return 1;
    }

    return 0;
}

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

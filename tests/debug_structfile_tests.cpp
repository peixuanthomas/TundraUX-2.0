#include "crypto.hpp"
#include "debug.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

void hello() {}

namespace {

void writeStoredString(std::ofstream& file, const std::string& value) {
    const std::size_t length = value.size();
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    file.write(value.data(), static_cast<std::streamsize>(length));
}

bool writeUserDataFile() {
    std::ofstream file("user_data.dat", std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    const int version = 21;
    const std::uint8_t strictMode = 1;
    const std::size_t userCount = 1;
    const int failedCount = 3;

    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&strictMode), sizeof(strictMode));
    file.write(reinterpret_cast<const char*>(&userCount), sizeof(userCount));
    writeStoredString(file, "admin");
    writeStoredString(file, "alice");
    writeStoredString(file, encrypt("Secret1"));
    writeStoredString(file, "hint");
    file.write(reinterpret_cast<const char*>(&failedCount), sizeof(failedCount));
    return static_cast<bool>(file);
}

bool structFileOutputsRawHeaderFields() {
    namespace fs = std::filesystem;

    const fs::path originalCwd = fs::current_path();
    const fs::path workspace = originalCwd / "debug_structfile_test_workspace";
    std::error_code error;
    fs::remove_all(workspace, error);
    fs::create_directories(workspace, error);
    if (error) {
        std::cerr << "failed to create test workspace: " << error.message() << "\n";
        return false;
    }

    fs::current_path(workspace, error);
    if (error) {
        std::cerr << "failed to enter test workspace: " << error.message() << "\n";
        return false;
    }

    if (!writeUserDataFile()) {
        std::cerr << "failed to write user data fixture\n";
        fs::current_path(originalCwd);
        fs::remove_all(workspace, error);
        return false;
    }

    std::ostringstream captured;
    auto* previousBuffer = std::cout.rdbuf(captured.rdbuf());
    struct_file();
    std::cout.rdbuf(previousBuffer);

    fs::current_path(originalCwd);
    fs::remove_all(workspace, error);

    const std::string output = captured.str();
    if (output.rfind("21\n1\n1\n", 0) != 0) {
        std::cerr << "struct_file header is not raw: " << output << "\n";
        return false;
    }
    if (output.find("marker=") != std::string::npos) {
        std::cerr << "struct_file output contains marker label: " << output << "\n";
        return false;
    }
    if (output.find("strict=") != std::string::npos) {
        std::cerr << "struct_file output contains strict label: " << output << "\n";
        return false;
    }

    std::vector<std::string> lines;
    std::istringstream outputStream(output);
    std::string line;
    while (std::getline(outputStream, line)) {
        lines.push_back(line);
    }
    if (lines.size() < 8 || lines[5] != "Secret1") {
        std::cerr << "struct_file did not decrypt the password field: " << output << "\n";
        return false;
    }
    return true;
}

bool structFileCommandIsDisabledInBackendMode() {
    std::ostringstream captured;
    auto* previousBuffer = std::cout.rdbuf(captured.rdbuf());
    handleDebugStructFileCommand("dbg:structfile", true);
    std::cout.rdbuf(previousBuffer);

    const std::string output = captured.str();
    if (output.find("dbg:structfile is not available in backend-separated mode") == std::string::npos) {
        std::cerr << "backend mode structfile message mismatch: " << output << "\n";
        return false;
    }
    if (output.find("user_data.dat") != std::string::npos) {
        std::cerr << "backend mode structfile should not inspect user_data.dat: " << output << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    return structFileOutputsRawHeaderFields() && structFileCommandIsDisabledInBackendMode() ? 0 : 1;
}

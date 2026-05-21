#include "explorer_permissions.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

bool can_modify_tux_file(const std::string&, const std::string&, const std::string&) {
    return true;
}

bool can_read_tux_file(const std::string&, const std::string&, const std::string&) {
    return true;
}

bool directory_has_protected_tux_files(const std::string&, const std::string&, const std::string&) {
    return false;
}

namespace {

tundraux::explorer::ExplorerState stateFor(const std::filesystem::path& root, std::string usertype) {
    tundraux::explorer::ExplorerState state;
    state.rootPath = root;
    state.currentPath = root;
    state.username = "tester";
    state.usertype = std::move(usertype);
    return state;
}

tundraux::explorer::FileEntry fileEntry(const std::filesystem::path& path) {
    tundraux::explorer::FileEntry entry;
    entry.name = path.filename().u8string();
    entry.path = path;
    entry.isDirectory = false;
    return entry;
}

tundraux::explorer::FileEntry directoryEntry(const std::filesystem::path& path) {
    tundraux::explorer::FileEntry entry;
    entry.name = path.filename().u8string();
    entry.path = path;
    entry.isDirectory = true;
    return entry;
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    const fs::path root = fs::current_path() / "explorer_permissions_test_workspace";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "Logs", error);
    if (error) {
        std::cerr << "failed to create test workspace: " << error.message() << "\n";
        return 1;
    }

    const fs::path auditLog = root / "Logs" / "audit-test.tlog";
    {
        std::ofstream out(auditLog, std::ios::binary | std::ios::trunc);
        out << "TLOG1";
    }

    const auto userState = stateFor(root, "user");
    const auto adminState = stateFor(root, "admin");
    const auto debugState = stateFor(root, "debug");

    if (tundraux::explorer::deletePermissionError(userState, fileEntry(auditLog)).empty()) {
        std::cerr << "regular user can mutate audit log file\n";
        return 1;
    }
    if (tundraux::explorer::deletePermissionError(adminState, fileEntry(auditLog)).empty()) {
        std::cerr << "admin can mutate audit log file\n";
        return 1;
    }
    if (!tundraux::explorer::deletePermissionError(debugState, fileEntry(auditLog)).empty()) {
        std::cerr << "debug user cannot mutate audit log file\n";
        return 1;
    }
    if (tundraux::explorer::deletePermissionError(userState, directoryEntry(root / "Logs")).empty()) {
        std::cerr << "regular user can mutate directory containing audit log\n";
        return 1;
    }

    fs::remove_all(root, error);
    return 0;
}

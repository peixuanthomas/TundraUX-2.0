#pragma once

#include <string>

void file_editor(const std::string& currentUsername, const std::string& currentUsertype);
int open_tux_file_in_editor(
    const std::string& tuxPath,
    const std::string& displayName,
    const std::string& currentUsername,
    const std::string& currentUsertype,
    bool allowReadOnly = false
);
bool can_modify_tux_file(
    const std::string& tuxPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
);
bool directory_has_protected_tux_files(
    const std::string& directoryPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
);

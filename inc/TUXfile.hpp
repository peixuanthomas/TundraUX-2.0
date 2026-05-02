#pragma once

#include <string>

void file_editor(const std::string& currentUsername, const std::string& currentUsertype);
int open_tux_file_in_editor(
    const std::string& tuxPath,
    const std::string& displayName,
    const std::string& currentUsername,
    const std::string& currentUsertype
);

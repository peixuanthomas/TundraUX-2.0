#pragma once

#include <string>

namespace tundraux::frontend {
class FrontendAuditSink;
}

int open_tux_file_in_editor(
    const std::string& tuxPath,
    const std::string& displayName,
    const std::string& currentUsername,
    const std::string& currentUsertype,
    bool allowReadOnly,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
bool can_modify_tux_file(
    const std::string& tuxPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
);
bool can_read_tux_file(
    const std::string& tuxPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
);
bool directory_has_protected_tux_files(
    const std::string& directoryPath,
    const std::string& currentUsername,
    const std::string& currentUsertype
);

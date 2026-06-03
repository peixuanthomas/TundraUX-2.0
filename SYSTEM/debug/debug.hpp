#pragma once

#include <string>

#include "backend_facade.hpp"

void delete_file();
void struct_file();
void display_test(const std::string& colorName = "");
void license();

void handleLicenseCommand(const std::string& input);
void handleDisplayTestCommand(const std::string& input);

// Debug-only utilities (not exposed in help)
void handleDebugCreateFileCommand(
    const std::string& input,
    bool backendMode = false
);
void handleDebugHelloCommand(const std::string& input);
void handleDebugDeleteFileCommand(
    const std::string& input,
    bool backendMode = false
);
void handleDebugStructFileCommand(
    const std::string& input,
    bool backendMode = false
);
void handleDebugEnvCommand(const std::string& input);
void handleDebugForceLoginCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    bool backendMode = false
);
void dbg_env();
void dbg_hexdump();
void dbg_resetfail(const std::string& username);

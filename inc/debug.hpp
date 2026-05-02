#pragma once

#include <string>

#include "udata.hpp"

void delete_file();
void struct_file();
void display_test();
void license();

void handleLicenseCommand(const std::string& input);
void handleDisplayTestCommand(const std::string& input);

// Debug-only utilities (not exposed in help)
void handleDebugEditorCommand(const std::string& input);
void handleDebugCreateFileCommand(const std::string& input);
void handleDebugHelloCommand(const std::string& input);
void handleDebugDeleteFileCommand(const std::string& input);
void handleDebugStructFileCommand(const std::string& input);
void handleDebugEnvCommand(const std::string& input);
void handleDebugForceLoginCommand(const std::string& input, USER& currentUser);
void dbg_env();
void dbg_hexdump();
void dbg_resetfail(const std::string& username);

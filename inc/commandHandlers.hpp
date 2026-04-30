#pragma once

#include <string>

#include "udata.hpp"

void handleLoginCommand(const std::string& input, USER& currentUser);
void handleExitCommand(const std::string& input);
void handleImportDataCommand(const std::string& input);
void handleTimeCommand(const std::string& input);
void handleModifyCommand(const std::string& input, USER& currentUser);
void handleClearScreenCommand(const std::string& input);
void handleLogoutCommand(const std::string& input, USER& currentUser);
void handleListUserCommand(const std::string& input);
void handleTuxFileCommand(const std::string& input, USER& currentUser);
void handleInfoCommand(const std::string& input);
void handleLicenseCommand(const std::string& input);
void handleManageUsersCommand(const std::string& input);
void handleEditCommand(const std::string& input);
void handleDebugEditorCommand(const std::string& input);
void handleDebugCreateFileCommand(const std::string& input);
void handleDebugHelloCommand(const std::string& input);
void handleDebugDeleteFileCommand(const std::string& input);
void handleDebugStructFileCommand(const std::string& input);
void handleDisplayTestCommand(const std::string& input);
void handleDebugEnvCommand(const std::string& input);
void handleDebugNewCommandCommand(const std::string& input);
void handleDebugForceLoginCommand(const std::string& input, USER& currentUser);

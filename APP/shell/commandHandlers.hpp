#pragma once

#include <string>

#include "udata.hpp"

void handleLoginCommand(const std::string& input, USER& currentUser);
void handleExitCommand(const std::string& input);
void handleImportDataCommand(const std::string& input);
void handleTimeCommand(const std::string& input);
void handleModifyCommand(const std::string& input, USER& currentUser);
void renderShellHeader();
void handleClearScreenCommand(const std::string& input);
void handleLogoutCommand(const std::string& input, USER& currentUser);
void handleListUserCommand(const std::string& input);
void handleTuxFileCommand(const std::string& input, USER& currentUser);
void handleInfoCommand(const std::string& input);
void handleManageUsersCommand(const std::string& input);
void handleEditCommand(const std::string& input);
void handleExplorerCommand(const std::string& input, USER& currentUser);
void handleWhoamiCommand(const USER& currentUser);

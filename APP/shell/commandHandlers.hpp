#pragma once

#include <string>

#include "udata.hpp"

namespace tundraux::frontend {
class BackendRuntime;
}

void handleLoginCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleExitCommand(const std::string& input);
void handleTimeCommand(const std::string& input);
void handleModifyCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void renderShellHeader();
void handleClearScreenCommand(const std::string& input);
void handleLogoutCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleListUserCommand(
    const std::string& input,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleInfoCommand(const std::string& input);
void handleManageUsersCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleEditCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleExplorerCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleWhoamiCommand(
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleStrictCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);
void handleExportCommand(
    const std::string& input,
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);

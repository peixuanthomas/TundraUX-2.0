#pragma once

#include <string>

#include "backend_facade.hpp"

namespace tundraux::frontend {
class BackendRuntime;
class FrontendAuditSink;
}

void handleLoginCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleExitCommand(const std::string& input);
void handleTimeCommand(const std::string& input);
void handleModifyCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void renderShellHeader();
void handleClearScreenCommand(const std::string& input);
void handleLogoutCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleListUserCommand(
    const std::string& input,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleInfoCommand(const std::string& input);
void handleManageUsersCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleEditCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleExplorerCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleWhoamiCommand(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleStrictCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
void handleExportCommand(
    const std::string& input,
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);

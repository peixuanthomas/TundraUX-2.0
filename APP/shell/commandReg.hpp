#pragma once

#include <functional>
#include <string>
#include <vector>

#include "backend_facade.hpp"

namespace tundraux::frontend {
class BackendRuntime;
class FrontendAuditSink;
}

using RegisteredCommandHandler = std::function<void(const std::string&)>;

struct RegisteredCommand {
    std::string name;
    std::string usage;
    std::string description;
    std::vector<std::string> aliases;
    RegisteredCommandHandler handler;
    std::string requiredUserType;
    bool hidden;
    bool allowArguments = false;
};

bool hasCommandPermission(const std::string& requiredUserType, const std::string& currentUserType);
std::vector<RegisteredCommand> buildNewCommandRegistry(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);
bool tryExecuteRegisteredCommand(
    const std::string& input,
    const std::vector<RegisteredCommand>& commands,
    const tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);

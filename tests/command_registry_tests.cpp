#include "commandReg.hpp"
#include "backend_runtime.hpp"

#include <iostream>
#include <string>
#include <vector>

bool tundraux::frontend::BackendRuntime::legacyDirect() const {
    return false;
}

void handleLoginCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void handleExitCommand(const std::string&) {}
void handleImportDataCommand(const std::string&, tundraux::frontend::BackendRuntime*) {}
void handleTimeCommand(const std::string&) {}
void handleModifyCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void renderShellHeader() {}
void handleClearScreenCommand(const std::string&) {}
void handleLogoutCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void handleListUserCommand(const std::string&, tundraux::frontend::BackendRuntime*) {}
void handleInfoCommand(const std::string&) {}
void handleManageUsersCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void handleEditCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void handleExplorerCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void handleWhoamiCommand(USER&, tundraux::frontend::BackendRuntime*) {}
void handleStrictCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void handleExportCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*) {}
void handleLicenseCommand(const std::string&) {}
void handleDisplayTestCommand(const std::string&) {}
void handleDebugEditorCommand(const std::string&) {}
void handleDebugCreateFileCommand(const std::string&) {}
void handleDebugHelloCommand(const std::string&) {}
void handleDebugDeleteFileCommand(const std::string&) {}
void handleDebugStructFileCommand(const std::string&) {}
void handleDebugEnvCommand(const std::string&) {}
void handleDebugForceLoginCommand(const std::string&, USER&) {}

namespace {

bool hasCommandNamed(const std::vector<RegisteredCommand>& commands, const std::string& name) {
    for (const auto& command : commands) {
        if (command.name == name) {
            return true;
        }
    }
    return false;
}

bool commandRegistryDropsRemovedCommands() {
    USER user;
    user.name = "debug";
    user.type = "debug";

    const auto commands = buildNewCommandRegistry(user, nullptr);
    if (hasCommandNamed(commands, "importdata")) {
        std::cerr << "importdata command should not be registered\n";
        return false;
    }
    if (hasCommandNamed(commands, "dbg:editor")) {
        std::cerr << "dbg:editor command should not be registered\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    return commandRegistryDropsRemovedCommands() ? 0 : 1;
}

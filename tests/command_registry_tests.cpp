#include "commandReg.hpp"
#include "backend_runtime.hpp"
#include "backend_facade.hpp"
#include "command_key_audit.hpp"

#include <iostream>
#include <string>
#include <vector>

bool g_backendRuntimeLegacyDirect = false;
bool g_createfileBackendMode = false;
bool g_deletefileBackendMode = false;
bool g_structfileBackendMode = false;
bool g_forceloginBackendMode = false;

void resetDebugCommandModeFlags() {
    g_createfileBackendMode = false;
    g_deletefileBackendMode = false;
    g_structfileBackendMode = false;
    g_forceloginBackendMode = false;
}

namespace tundraux::frontend {
class BackendProcessTransport {
};

BackendRuntime::BackendRuntime() {}
BackendRuntime::~BackendRuntime() {}

bool BackendRuntime::legacyDirect() const {
    return g_backendRuntimeLegacyDirect;
}

class FrontendAuditSink;
}

void handleLoginCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleExitCommand(const std::string&) {}
void handleImportDataCommand(const std::string&, tundraux::frontend::BackendRuntime*) {}
void handleTimeCommand(const std::string&) {}
void handleModifyCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void renderShellHeader() {}
void handleClearScreenCommand(const std::string&) {}
void handleLogoutCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleListUserCommand(const std::string&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleInfoCommand(const std::string&) {}
void handleManageUsersCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleEditCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleExplorerCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleWhoamiCommand(USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleStrictCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleExportCommand(const std::string&, USER&, tundraux::frontend::BackendRuntime*, tundraux::frontend::FrontendAuditSink*) {}
void handleLicenseCommand(const std::string&) {}
void handleDisplayTestCommand(const std::string&) {}
void handleDebugEditorCommand(const std::string&) {}
void handleDebugHelloCommand(const std::string&) {}
void handleDebugEnvCommand(const std::string&) {}

void handleDebugCreateFileCommand(const std::string&, bool backendMode) {
    g_createfileBackendMode = backendMode;
}

void handleDebugDeleteFileCommand(const std::string&, bool backendMode) {
    g_deletefileBackendMode = backendMode;
}

void handleDebugStructFileCommand(const std::string&, bool backendMode) {
    g_structfileBackendMode = backendMode;
}

void handleDebugForceLoginCommand(const std::string&, USER&, bool backendMode) {
    g_forceloginBackendMode = backendMode;
}

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

struct FakeFrontendAuditSink final : tundraux::frontend::FrontendAuditSink {
    int eventCount = 0;
    std::vector<std::string> events;
    int keyCount = 0;
    int setUserCount = 0;
    tundraux::frontend::ShellUser lastUser;

    void setCurrentUser(const tundraux::frontend::ShellUser& user) override {
        ++setUserCount;
        lastUser = user;
    }

    tundraux::frontend::FacadeResult logEvent(
        const std::string& category,
        const std::string& detail
    ) override {
        ++eventCount;
        events.push_back(category + ":" + detail);
        return {true, "", ""};
    }

    tundraux::frontend::FacadeResult logKeyPress(const std::string&, bool) override {
        ++keyCount;
        return {true, "", ""};
    }
};

bool commandRegistryLogsDeniedCommandsInNonBackendMode() {
    USER user;
    user.type = "guest";
    user.name = "visitor";

    FakeFrontendAuditSink auditSink;
    const auto commands = buildNewCommandRegistry(user, nullptr, &auditSink);

    if (!tryExecuteRegisteredCommand("explorer", commands, user, &auditSink)) {
        std::cerr << "explorer command was not found in registry\n";
        return false;
    }

    if (auditSink.eventCount != 1) {
        std::cerr << "expected exactly one command audit event in denied path, got " << auditSink.eventCount << "\n";
        return false;
    }
    if (auditSink.events.empty() || auditSink.events[0].find("command:denied explorer") == std::string::npos) {
        std::cerr << "expected denied explorer audit event, got: ";
        if (auditSink.events.empty()) {
            std::cerr << "<none>\n";
        } else {
            std::cerr << auditSink.events[0] << "\n";
        }
        return false;
    }
    if (auditSink.setUserCount == 0) {
        std::cerr << "expected sink to receive setCurrentUser\n";
        return false;
    }

    return true;
}

bool commandKeyAuditMappingRoundTrip() {
    using tundra_tui::Key;
    using tundraux::frontend::keyPressFromFrontendAuditText;
    using tundraux::frontend::toFrontendAuditKeyText;

    if (toFrontendAuditKeyText({Key::Character, 'x'}) != "x") {
        std::cerr << "expected character key to map to literal text\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::Enter, '\0'}) != "Enter") {
        std::cerr << "expected Enter key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::Backspace, '\0'}) != "Backspace") {
        std::cerr << "expected Backspace key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::Delete, '\0'}) != "Delete") {
        std::cerr << "expected Delete key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::PageDown, '\0'}) != "PageDown") {
        std::cerr << "expected PageDown key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::F1, '\0'}) != "F1" || toFrontendAuditKeyText({Key::F2, '\0'}) != "F2") {
        std::cerr << "expected F1/F2 key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::Escape, '\0'}) != "Escape") {
        std::cerr << "expected Escape key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::Tab, '\0'}) != "Tab") {
        std::cerr << "expected Tab key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::Home, '\0'}) != "Home") {
        std::cerr << "expected Home key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::End, '\0'}) != "End") {
        std::cerr << "expected End key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::PageUp, '\0'}) != "PageUp") {
        std::cerr << "expected PageUp key mapping\n";
        return false;
    }
    if (toFrontendAuditKeyText({Key::Up, '\0'}) != "Up" ||
        toFrontendAuditKeyText({Key::Down, '\0'}) != "Down" ||
        toFrontendAuditKeyText({Key::Left, '\0'}) != "Left" ||
        toFrontendAuditKeyText({Key::Right, '\0'}) != "Right") {
        std::cerr << "expected arrow key mapping\n";
        return false;
    }

    const auto enterPress = keyPressFromFrontendAuditText("Enter");
    if (enterPress.key != Key::Enter) {
        std::cerr << "expected Enter text to map back to Enter key\n";
        return false;
    }
    const auto charPress = keyPressFromFrontendAuditText("a");
    if (charPress.key != Key::Character || charPress.character != 'a') {
        std::cerr << "expected char text to map to Character key with same character\n";
        return false;
    }
    const auto unknownPress = keyPressFromFrontendAuditText("Unknown");
    if (unknownPress.key != Key::Unknown) {
        std::cerr << "expected Unknown text to map to Unknown key\n";
        return false;
    }
    if (keyPressFromFrontendAuditText("Escape").key != Key::Escape) {
        std::cerr << "expected Escape text to map back to Escape key\n";
        return false;
    }
    if (keyPressFromFrontendAuditText("Tab").key != Key::Tab) {
        std::cerr << "expected Tab text to map back to Tab key\n";
        return false;
    }
    if (keyPressFromFrontendAuditText("Home").key != Key::Home) {
        std::cerr << "expected Home text to map back to Home key\n";
        return false;
    }
    if (keyPressFromFrontendAuditText("End").key != Key::End) {
        std::cerr << "expected End text to map back to End key\n";
        return false;
    }
    if (keyPressFromFrontendAuditText("PageUp").key != Key::PageUp) {
        std::cerr << "expected PageUp text to map back to PageUp key\n";
        return false;
    }
    if (keyPressFromFrontendAuditText("Up").key != Key::Up ||
        keyPressFromFrontendAuditText("Down").key != Key::Down ||
        keyPressFromFrontendAuditText("Left").key != Key::Left ||
        keyPressFromFrontendAuditText("Right").key != Key::Right) {
        std::cerr << "expected arrow key text to map back to arrow keys\n";
        return false;
    }

    return true;
}

bool commandRegistryRoutesBackendModeToDebugCommands() {
    USER user;
    user.type = "debug";
    user.name = "debugger";

    struct LegacyModeGuard {
        bool previousValue;

        explicit LegacyModeGuard(bool nextValue)
            : previousValue(g_backendRuntimeLegacyDirect) {
            g_backendRuntimeLegacyDirect = nextValue;
        }

        ~LegacyModeGuard() {
            g_backendRuntimeLegacyDirect = previousValue;
        }
    };

    tundraux::frontend::BackendRuntime runtime;

    {
        LegacyModeGuard backendGuard(false);
        resetDebugCommandModeFlags();

        auto backendModeCommands = buildNewCommandRegistry(user, &runtime);
        if (!tryExecuteRegisteredCommand("dbg:createfile", backendModeCommands, user, nullptr)) {
            std::cerr << "dbg:createfile command was not found in backend-mode registry\n";
            return false;
        }
        if (!g_createfileBackendMode) {
            std::cerr << "expected dbg:createfile backend mode flag true\n";
            return false;
        }
        if (!tryExecuteRegisteredCommand("dbg:deletefile", backendModeCommands, user, nullptr)) {
            std::cerr << "dbg:deletefile command was not found in backend-mode registry\n";
            return false;
        }
        if (!g_deletefileBackendMode) {
            std::cerr << "expected dbg:deletefile backend mode flag true\n";
            return false;
        }
        if (!tryExecuteRegisteredCommand("dbg:structfile", backendModeCommands, user, nullptr)) {
            std::cerr << "dbg:structfile command was not found in backend-mode registry\n";
            return false;
        }
        if (!g_structfileBackendMode) {
            std::cerr << "expected dbg:structfile backend mode flag true\n";
            return false;
        }
        if (!tryExecuteRegisteredCommand("dbg:forcelogin test_user", backendModeCommands, user, nullptr)) {
            std::cerr << "dbg:forcelogin command was not found in backend-mode registry\n";
            return false;
        }
        if (!g_forceloginBackendMode) {
            std::cerr << "expected dbg:forcelogin backend mode flag true\n";
            return false;
        }
    }

    {
        LegacyModeGuard legacyDirectGuard(true);
        resetDebugCommandModeFlags();

        auto legacyDirectCommands = buildNewCommandRegistry(user, &runtime);
        if (!tryExecuteRegisteredCommand("dbg:createfile", legacyDirectCommands, user, nullptr)) {
            std::cerr << "dbg:createfile command was not found in legacy-direct-mode registry\n";
            return false;
        }
        if (g_createfileBackendMode) {
            std::cerr << "expected dbg:createfile backend mode flag false for legacy-direct mode\n";
            return false;
        }
        if (!tryExecuteRegisteredCommand("dbg:deletefile", legacyDirectCommands, user, nullptr)) {
            std::cerr << "dbg:deletefile command was not found in legacy-direct-mode registry\n";
            return false;
        }
        if (g_deletefileBackendMode) {
            std::cerr << "expected dbg:deletefile backend mode flag false for legacy-direct mode\n";
            return false;
        }
        if (!tryExecuteRegisteredCommand("dbg:structfile", legacyDirectCommands, user, nullptr)) {
            std::cerr << "dbg:structfile command was not found in legacy-direct-mode registry\n";
            return false;
        }
        if (g_structfileBackendMode) {
            std::cerr << "expected dbg:structfile backend mode flag false for legacy-direct mode\n";
            return false;
        }
        if (!tryExecuteRegisteredCommand("dbg:forcelogin test_user", legacyDirectCommands, user, nullptr)) {
            std::cerr << "dbg:forcelogin command was not found in legacy-direct-mode registry\n";
            return false;
        }
        if (g_forceloginBackendMode) {
            std::cerr << "expected dbg:forcelogin backend mode flag false for legacy-direct mode\n";
            return false;
        }
    }

    {
        LegacyModeGuard legacyNullGuard(false);
        resetDebugCommandModeFlags();

        auto legacyCommands = buildNewCommandRegistry(user, nullptr);
        if (!tryExecuteRegisteredCommand("dbg:createfile", legacyCommands, user, nullptr)) {
            std::cerr << "dbg:createfile command was not found in legacy-mode registry\n";
            return false;
        }
        if (g_createfileBackendMode) {
            std::cerr << "expected dbg:createfile backend mode flag false for legacy mode\n";
            return false;
        }

        if (!tryExecuteRegisteredCommand("dbg:deletefile", legacyCommands, user, nullptr)) {
            std::cerr << "dbg:deletefile command was not found in legacy-mode registry\n";
            return false;
        }
        if (g_deletefileBackendMode) {
            std::cerr << "expected dbg:deletefile backend mode flag false for legacy mode\n";
            return false;
        }

        if (!tryExecuteRegisteredCommand("dbg:structfile", legacyCommands, user, nullptr)) {
            std::cerr << "dbg:structfile command was not found in legacy-mode registry\n";
            return false;
        }
        if (g_structfileBackendMode) {
            std::cerr << "expected dbg:structfile backend mode flag false for legacy mode\n";
            return false;
        }

        if (!tryExecuteRegisteredCommand("dbg:forcelogin test_user", legacyCommands, user, nullptr)) {
            std::cerr << "dbg:forcelogin command was not found in legacy-mode registry\n";
            return false;
        }
        if (g_forceloginBackendMode) {
            std::cerr << "expected dbg:forcelogin backend mode flag false for legacy mode\n";
            return false;
        }

        return true;
    }

}

} // namespace

int main() {
    if (!commandRegistryDropsRemovedCommands()) {
        return 1;
    }
    if (!commandRegistryLogsDeniedCommandsInNonBackendMode()) {
        return 1;
    }
    if (!commandKeyAuditMappingRoundTrip()) {
        return 1;
    }
    if (!commandRegistryRoutesBackendModeToDebugCommands()) {
        return 1;
    }
    return 0;
}

#include "commandReg.hpp"

#include "backend_runtime.hpp"
#include "commandHandlers.hpp"
#include "debug.hpp"

std::vector<RegisteredCommand> buildNewCommandRegistry(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    const bool backendMode = backendRuntime != nullptr && !backendRuntime->legacyDirect();

    return {
        {
            "help",
            "help",
            "Show this help message",
            {"?"},
            [](const std::string&) {},
            "",
            false
        },
        {
            "exit",
            "exit",
            "Exit the program",
            {"quit", "q"},
            handleExitCommand,
            "",
            false
        },
        {
            "cls",
            "cls",
            "Clear the screen",
            {"clear"},
            handleClearScreenCommand,
            "",
            false
        },
        {
            "login",
            "login <username>",
            "Log in as specified user",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleLoginCommand(input, currentUser, backendRuntime, auditSink);
            },
            "guest,debug",
            false,
            true
        },
        {
            "logout",
            "logout",
            "Log out current user",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleLogoutCommand(input, currentUser, backendRuntime, auditSink);
            },
            "",
            false
        },
        {
            "listuser",
            "listuser",
            "List all users",
            {},
            [auditSink, backendRuntime](const std::string& input) {
                handleListUserCommand(input, backendRuntime, auditSink);
            },
            backendRuntime != nullptr && !backendRuntime->legacyDirect() ? "admin,debug" : "",
            false
        },
        {
            "manageuser",
            "manageuser",
            "Open user management interface",
            {"manageusers"},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleManageUsersCommand(input, currentUser, backendRuntime, auditSink);
            },
            "admin,debug",
            false
        },
        {
            "modify",
            "modify",
            "Modify current user information",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleModifyCommand(input, currentUser, backendRuntime, auditSink);
            },
            "",
            false
        },
        {
            "time",
            "time",
            "Display current system time and timestamp",
            {},
            handleTimeCommand,
            "",
            false
        },
        {
            "license",
            "license",
            "Show terms of use license",
            {},
            handleLicenseCommand,
            "",
            false
        },
        {
            "info",
            "info",
            "Show program information",
            {},
            handleInfoCommand,
            "",
            false
        },
        {
            "strict",
            "strict <status|on|off>",
            "View or change strict audit mode",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleStrictCommand(input, currentUser, backendRuntime, auditSink);
            },
            "admin,debug",
            false,
            true
        },
        {
            "export",
            "export log <tlog-file>",
            "Export an encrypted audit log to plaintext",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleExportCommand(input, currentUser, backendRuntime, auditSink);
            },
            "admin,debug",
            false,
            true
        },
        {
            "edit",
            "edit [filename]",
            "Open the text editor",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleEditCommand(input, currentUser, backendRuntime, auditSink);
            },
            "admin,user,debug",
            false,
            true
        },
        {
            "explorer",
            "explorer",
            "Open the file explorer",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string& input) {
                handleExplorerCommand(input, currentUser, backendRuntime, auditSink);
            },
            "admin,user,debug",
            false
        },
        {
            "dbg:help",
            "dbg:help",
            "Show debug commands",
            {},
            [](const std::string&) {},
            "debug",
            true
        },
        {
            "dbg:createfile",
            "dbg:createfile",
            "Create user data file",
            {},
            [backendMode](const std::string& input) {
                handleDebugCreateFileCommand(input, backendMode);
            },
            "debug",
            true
        },
        {
            "dbg:hello()",
            "dbg:hello()",
            "Run hello debug command",
            {},
            handleDebugHelloCommand,
            "debug",
            true
        },
        {
            "dbg:deletefile",
            "dbg:deletefile",
            "Delete debug file",
            {},
            [backendMode](const std::string& input) {
                handleDebugDeleteFileCommand(input, backendMode);
            },
            "debug",
            true
        },
        {
            "dbg:structfile",
            "dbg:structfile",
            "Show file structure debug output",
            {},
            [backendMode](const std::string& input) {
                handleDebugStructFileCommand(input, backendMode);
            },
            "debug",
            true
        },
        {
            "dbg:env",
            "dbg:env",
            "Show debug environment information",
            {},
            handleDebugEnvCommand,
            "debug",
            true
        },
        {
            "dbg:displaytest",
            "dbg:displaytest [color]",
            "Run display color test",
            {},
            handleDisplayTestCommand,
            "debug",
            true,
            true
        },
        {
            "dbg:forcelogin",
            "dbg:forcelogin <username>",
            "Force login as a user",
            {},
            [&currentUser, backendRuntime](const std::string& input) {
                handleDebugForceLoginCommand(input, currentUser, backendRuntime);
            },
            "debug",
            true,
            true
        },
        {
            "whoami",
            "whoami",
            "Display the current logged in user",
            {},
            [auditSink, &currentUser, backendRuntime](const std::string&) {
                handleWhoamiCommand(currentUser, backendRuntime, auditSink);
            },
            "",
            false
        }
    };
}

#include "commandReg.hpp"

#include "backend_runtime.hpp"
#include "commandHandlers.hpp"
#include "debug.hpp"

std::vector<RegisteredCommand> buildNewCommandRegistry(
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime
) {
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
            [&currentUser, backendRuntime](const std::string& input) {
                handleLoginCommand(input, currentUser, backendRuntime);
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
            [&currentUser, backendRuntime](const std::string& input) {
                handleLogoutCommand(input, currentUser, backendRuntime);
            },
            "",
            false
        },
        {
            "listuser",
            "listuser",
            "List all users",
            {},
            [backendRuntime](const std::string& input) {
                handleListUserCommand(input, backendRuntime);
            },
            backendRuntime != nullptr && !backendRuntime->legacyDirect() ? "admin,debug" : "",
            false
        },
        {
            "manageuser",
            "manageuser",
            "Open user management interface",
            {"manageusers"},
            [&currentUser, backendRuntime](const std::string& input) {
                handleManageUsersCommand(input, currentUser, backendRuntime);
            },
            "admin,debug",
            false
        },
        {
            "modify",
            "modify",
            "Modify current user information",
            {},
            [&currentUser, backendRuntime](const std::string& input) {
                handleModifyCommand(input, currentUser, backendRuntime);
            },
            "",
            false
        },
        {
            "importdata",
            "importdata",
            "Import user data from old versions",
            {},
            [backendRuntime](const std::string& input) {
                handleImportDataCommand(input, backendRuntime);
            },
            "admin,debug",
            false
        },
        {
            "TUXfile",
            "TUXfile",
            "Open TUX File Manager (Terminal style)",
            {"tuxfile"},
            [&currentUser, backendRuntime](const std::string& input) {
                handleTuxFileCommand(input, currentUser, backendRuntime);
            },
            "user,admin,debug",
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
            [&currentUser, backendRuntime](const std::string& input) {
                handleStrictCommand(input, currentUser, backendRuntime);
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
            [&currentUser, backendRuntime](const std::string& input) {
                handleExportCommand(input, currentUser, backendRuntime);
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
            [&currentUser, backendRuntime](const std::string& input) {
                handleEditCommand(input, currentUser, backendRuntime);
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
            [&currentUser, backendRuntime](const std::string &input) {
                handleExplorerCommand(input, currentUser, backendRuntime);
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
            "dbg:editor",
            "dbg:editor [backend]",
            "Inspect or change editor backend",
            {},
            handleDebugEditorCommand,
            "debug",
            true,
            true
        },
        {
            "dbg:createfile",
            "dbg:createfile",
            "Create user data file",
            {},
            handleDebugCreateFileCommand,
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
            handleDebugDeleteFileCommand,
            "debug",
            true
        },
        {
            "dbg:structfile",
            "dbg:structfile",
            "Show file structure debug output",
            {},
            handleDebugStructFileCommand,
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
            [&currentUser](const std::string& input) { handleDebugForceLoginCommand(input, currentUser); },
            "debug",
            true,
            true
        },
        {
            "whoami",
            "whoami",
            "Display the current logged in user",
            {},
            [&currentUser, backendRuntime](const std::string&) {
                handleWhoamiCommand(currentUser, backendRuntime);
            },
            "",
            false
        }
    };
}

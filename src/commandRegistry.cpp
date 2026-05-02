#include "commandReg.hpp"

#include "commandHandlers.hpp"
#include "debug.hpp"

std::vector<RegisteredCommand> buildNewCommandRegistry(USER& currentUser) {
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
            [&currentUser](const std::string& input) { handleLoginCommand(input, currentUser); },
            "guest,debug",
            false,
            true
        },
        {
            "logout",
            "logout",
            "Log out current user",
            {},
            [&currentUser](const std::string& input) { handleLogoutCommand(input, currentUser); },
            "",
            false
        },
        {
            "listuser",
            "listuser",
            "List all users",
            {},
            handleListUserCommand,
            "",
            false
        },
        {
            "manageuser",
            "manageuser",
            "Open user management interface",
            {"manageusers"},
            handleManageUsersCommand,
            "admin,debug",
            false
        },
        {
            "modify",
            "modify",
            "Modify current user information",
            {},
            [&currentUser](const std::string& input) { handleModifyCommand(input, currentUser); },
            "",
            false
        },
        {
            "importdata",
            "importdata",
            "Import user data from old versions",
            {},
            handleImportDataCommand,
            "admin,debug",
            false
        },
        {
            "TUXfile",
            "TUXfile",
            "Open TUX File Manager",
            {"tuxfile"},
            [&currentUser](const std::string& input) { handleTuxFileCommand(input, currentUser); },
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
            "displaytest",
            "displaytest",
            "Run display test",
            {},
            handleDisplayTestCommand,
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
            "edit",
            "edit [filename]",
            "Open the text editor",
            {},
            handleEditCommand,
            "admin,user,debug",
            false,
            true
        },
        {
            "explorer",
            "explorer",
            "Open the file explorer",
            {},
            [&currentUser](const std::string &input) { handleExplorerCommand(input, currentUser); },
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
            "dbg:forcelogin",
            "dbg:forcelogin <username>",
            "Force login as a user",
            {},
            [&currentUser](const std::string& input) { handleDebugForceLoginCommand(input, currentUser); },
            "debug",
            true,
            true
        }
    };
}

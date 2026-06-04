#include <iostream>
#include "backend_runtime.hpp"
#include "backend_facade.hpp"
#include "hello.hpp"
#include "color.hpp"
#include "command.hpp"
#include <cstdlib>
#include <string>
#include <fstream>

#ifndef TUNDRAUX_DEFAULT_USER_TYPE
#define TUNDRAUX_DEFAULT_USER_TYPE "guest"
#endif

#ifndef TUNDRAUX_DEFAULT_USER_NAME
#define TUNDRAUX_DEFAULT_USER_NAME ""
#endif

namespace {

tundraux::frontend::BackendRuntime* backendRuntimeForExit = nullptr;

void shutdownBackendRuntimeForExit() {
    if (backendRuntimeForExit != nullptr) {
        backendRuntimeForExit->shutdown();
        backendRuntimeForExit = nullptr;
    }
}

void printUsage() {
    colorcout("red", "Usage: TundraUX2 [--backend-stdio <path>]\n");
}

bool parseArgs(int argc, char* argv[], tundraux::frontend::BackendRuntimeOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--legacy-direct") {
            return false;
        } else if (arg == "--backend-stdio") {
            if (i + 1 >= argc) {
                return false;
            }
            options.backendStdioPath = argv[++i];
            if (options.backendStdioPath.empty()) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

int enterShell(tundraux::frontend::BackendRuntime& backendRuntime, const tundraux::frontend::BackendRuntimeOptions& options) {
    std::string error;
    if (!backendRuntime.initialize(options, error)) {
        colorcout("red", error + "\n");
        pause();
        backendRuntimeForExit = nullptr;
        return 1;
    }

    task_main(&backendRuntime);
    backendRuntime.shutdown();
    backendRuntimeForExit = nullptr;
    return 0;
}

void displayLicense(std::ifstream& licenseFile) {
    std::string line;
    while (std::getline(licenseFile, line)) {
        colorcout("white", line + "\n");
    }
    licenseFile.close();
    colorcout("yellow", "\nPress Enter to accept the license and continue...");
    std::cin.get();
    clear_screen();
}

void abortStartupWithMessage(tundraux::frontend::BackendRuntime& backendRuntime, const std::string& message) {
    colorcout("red", message + "\n");
    pause();
    backendRuntime.shutdown();
    backendRuntimeForExit = nullptr;
}

bool setupAlreadyInitialized(const tundraux::frontend::FacadeResult& result) {
    return !result.ok &&
        result.errorCode == "PermissionDenied" &&
        result.message == "Setup already initialized.";
}

bool backendSetupRequired(tundraux::frontend::BackendFacade& facade, std::string& error) {
    error.clear();
    const auto result = facade.createInitialAdmin("null", "Secret1", "setup probe");
    if (setupAlreadyInitialized(result)) {
        return false;
    }
    if (!result.ok && result.errorCode == "InvalidParams") {
        return true;
    }

    error = result.message.empty() ? "Failed to determine setup state." : result.message;
    return false;
}

bool legacySetupRequired(std::string& error) {
    error = "Legacy direct setup is not available in the backend-separated frontend.";
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    tundraux::frontend::BackendRuntimeOptions backendOptions;
    backendOptions.startupUserType = TUNDRAUX_DEFAULT_USER_TYPE;
    backendOptions.startupUserName = TUNDRAUX_DEFAULT_USER_NAME;
    if (!parseArgs(argc, argv, backendOptions)) {
        printUsage();
        pause();
        return 1;
    }

    tundraux::frontend::BackendRuntime backendRuntime;
    backendRuntimeForExit = &backendRuntime;
    std::atexit(shutdownBackendRuntimeForExit);

    std::string backendError;
    if (!backendRuntime.initialize(backendOptions, backendError)) {
        colorcout("red", backendError + "\n");
        pause();
        backendRuntimeForExit = nullptr;
        return 1;
    }

    std::ifstream licenseFile("license");
    std::string setupError;
    bool setupRequired = false;
    tundraux::frontend::BackendFacade facade(backendRuntime);
    setupRequired = backendSetupRequired(facade, setupError);
    if (setupRequired) {
        if (!licenseFile) {
            abortStartupWithMessage(backendRuntime, "Critical file missing: license");
            return 1;
        }
        displayLicense(licenseFile);
        hello(facade);
    }

    if (!setupError.empty()) {
        abortStartupWithMessage(backendRuntime, setupError);
        return 1;
    }

    clear_screen();
    set_title("TundraUX 2.0");
    task_main(&backendRuntime);
    backendRuntime.shutdown();
    backendRuntimeForExit = nullptr;
    return 0;
}

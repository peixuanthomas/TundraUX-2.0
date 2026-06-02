#include <iostream>
#include "backend_runtime.hpp"
#include "hello.hpp"
#include "color.hpp"
#include "udata.hpp"
#include "command.hpp"
#include <cstdlib>
#include <filesystem>
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
    colorcout("red", "Usage: TundraUX2 [--legacy-direct] [--backend-stdio <path>]\n");
}

bool parseArgs(int argc, char* argv[], tundraux::frontend::BackendRuntimeOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--legacy-direct") {
            options.legacyDirect = true;
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

    std::ifstream licenseFile("license");
    std::ifstream detectData("user_data.dat");
    if (licenseFile && !detectData) {
        std::string line;
        while (std::getline(licenseFile, line)) {
            colorcout("white", line + "\n");
        }
        licenseFile.close();
        colorcout("yellow", "\nPress Enter to accept the license and continue...");
        std::cin.get();
        clear_screen();
        hello();
        return enterShell(backendRuntime, backendOptions);
    } else if (detectData) {
        detectData.close();
        clear_screen();
        set_title("TundraUX 2.0");
        return enterShell(backendRuntime, backendOptions);
    } else {
        colorcout("red", "Critical files missing! Program aborted.\n");
        pause();
        backendRuntimeForExit = nullptr;
        return 1;
    }
    colorcout("red", "Program has run into an unexpected place. You may need to contact developer for assistance.\n");
    backendRuntime.shutdown();
    backendRuntimeForExit = nullptr;
    pause();
    return 1;
}

#include "data_manager_user_store.hpp"
#include "file_service.hpp"
#include "filesystem_file_store.hpp"
#include "filesystem_tux_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "tux_service.hpp"
#include "user_service.hpp"

#include <iostream>
#include <string>
#include <utility>

namespace {

void printUsage() {
    std::cerr << "Usage: tundraux_backend_stdio [--user-data PATH] [--files-root PATH]\n";
}

bool parseArgs(int argc, char* argv[], std::string& userDataPath, std::string& filesRoot) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg != "--user-data" && arg != "--files-root") {
            printUsage();
            return false;
        }
        if (i + 1 >= argc) {
            printUsage();
            return false;
        }
        std::string value = argv[++i];
        if (value.empty()) {
            printUsage();
            return false;
        }
        if (arg == "--user-data") {
            userDataPath = std::move(value);
        } else {
            filesRoot = std::move(value);
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string userDataPath = "user_data.dat";
    std::string filesRoot = "Files";
    if (!parseArgs(argc, argv, userDataPath, filesRoot)) {
        return 1;
    }

    tundraux::backend::DataManagerUserStore usersStore(userDataPath);
    tundraux::backend::FilesystemFileStore filesStore(filesRoot);
    tundraux::backend::SessionService sessions(usersStore);
    tundraux::backend::UserService users(usersStore, sessions);
    tundraux::backend::FileService files(filesStore, sessions, usersStore);
    tundraux::backend::FilesystemTuxStore tuxStore(filesRoot);
    tundraux::backend::TuxService tux(tuxStore, sessions, usersStore);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users, files, tux);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::cout << dispatcher.handleLine(line) << std::endl;
    }

    return 0;
}

#include "data_manager_user_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <iostream>
#include <string>

namespace {

void printUsage() {
    std::cerr << "Usage: tundraux_backend_stdio [--user-data PATH]\n";
}

bool parseArgs(int argc, char* argv[], std::string& userDataPath) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg != "--user-data") {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage();
            return false;
        }
        if (i + 1 >= argc) {
            std::cerr << "--user-data requires a path.\n";
            printUsage();
            return false;
        }
        userDataPath = argv[++i];
        if (userDataPath.empty()) {
            std::cerr << "--user-data path must not be empty.\n";
            printUsage();
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string userDataPath = "user_data.dat";
    if (!parseArgs(argc, argv, userDataPath)) {
        return 1;
    }

    tundraux::backend::DataManagerUserStore store(userDataPath);
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::cout << dispatcher.handleLine(line) << std::endl;
    }

    return 0;
}

#include "data_manager_user_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string userDataPath = "user_data.dat";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--user-data") {
            userDataPath = argv[i + 1];
            ++i;
        }
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

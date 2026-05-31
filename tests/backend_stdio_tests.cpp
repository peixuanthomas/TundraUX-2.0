#include "data_manager_user_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <iostream>

int main() {
    tundraux::backend::DataManagerUserStore store("user_data.dat");
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users);

    const std::string response = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    if (response.find(R"("result")") == std::string::npos ||
        response.find(R"("sessionId")") == std::string::npos) {
        std::cerr << "stdio dispatcher smoke response missing session result: " << response << "\n";
        return 1;
    }
    return 0;
}

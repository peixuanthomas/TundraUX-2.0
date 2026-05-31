#include "data_manager_user_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

bool runGuestSessionSmokeTest() {
    tundraux::backend::DataManagerUserStore store("user_data.dat");
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users);

    const std::string response = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    if (response.find(R"("result")") == std::string::npos ||
        response.find(R"("sessionId")") == std::string::npos) {
        std::cerr << "stdio dispatcher smoke response missing session result: " << response << "\n";
        return false;
    }
    return true;
}

bool runMalformedUserDataTest() {
    const auto path = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_malformed_user_data.dat";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "not a user data file";
    }

    tundraux::backend::DataManagerUserStore store(path.string());
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users);
    dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");

    std::ostringstream capturedStdout;
    auto* previousStdout = std::cout.rdbuf(capturedStdout.rdbuf());
    const std::string response = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":"session-1","username":"alice","password":"Secret1"}})"
    );
    std::cout.rdbuf(previousStdout);

    std::filesystem::remove(path);

    if (!capturedStdout.str().empty()) {
        std::cerr << "malformed user data leaked stdout: " << capturedStdout.str() << "\n";
        return false;
    }
    if (response.find(R"("code":"StorageError")") == std::string::npos) {
        std::cerr << "malformed user data response missing StorageError: " << response << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!runGuestSessionSmokeTest()) {
        return 1;
    }
    if (!runMalformedUserDataTest()) {
        return 1;
    }

    return 0;
}

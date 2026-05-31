#include "data_manager_user_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::string quotePath(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

std::string startGuestSession(tundraux::backend::JsonRpcDispatcher& dispatcher) {
    const std::string response = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto parsed = tundraux::backend::parseJson(response);
    if (!parsed.ok) {
        return {};
    }
    return parsed.value.asObject().at("result").asObject().at("sessionId").asString();
}

bool responseHasStorageErrorWithNoStdoutLeak(const std::filesystem::path& path, const std::string& label) {
    tundraux::backend::DataManagerUserStore store(path.string());
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users);
    const std::string sessionId = startGuestSession(dispatcher);
    if (sessionId.empty()) {
        std::cerr << label << " could not start guest session\n";
        return false;
    }

    std::ostringstream capturedStdout;
    auto* previousStdout = std::cout.rdbuf(capturedStdout.rdbuf());
    const std::string response = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Secret1"}})"
    );
    std::cout.rdbuf(previousStdout);

    if (!capturedStdout.str().empty()) {
        std::cerr << label << " leaked stdout: " << capturedStdout.str() << "\n";
        return false;
    }
    if (response.find(R"("code":"StorageError")") == std::string::npos) {
        std::cerr << label << " response missing StorageError: " << response << "\n";
        return false;
    }
    return true;
}

bool runInvalidStorageInProcessTests() {
    const auto malformedPath = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_malformed_user_data.dat";
    {
        std::ofstream file(malformedPath, std::ios::binary | std::ios::trunc);
        file << "not a user data file";
    }
    if (!responseHasStorageErrorWithNoStdoutLeak(malformedPath, "malformed user data")) {
        std::filesystem::remove(malformedPath);
        return false;
    }
    std::filesystem::remove(malformedPath);

    const auto emptyPath = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_empty_user_data.dat";
    {
        std::ofstream file(emptyPath, std::ios::binary | std::ios::trunc);
    }
    if (!responseHasStorageErrorWithNoStdoutLeak(emptyPath, "empty user data")) {
        std::filesystem::remove(emptyPath);
        return false;
    }
    std::filesystem::remove(emptyPath);

    const auto directoryPath = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_user_data_dir";
    std::filesystem::remove_all(directoryPath);
    std::filesystem::create_directory(directoryPath);
    if (!responseHasStorageErrorWithNoStdoutLeak(directoryPath, "directory user data path")) {
        std::filesystem::remove_all(directoryPath);
        return false;
    }
    std::filesystem::remove_all(directoryPath);

    const auto missingPath = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_missing_user_data.dat";
    std::filesystem::remove(missingPath);
    if (!responseHasStorageErrorWithNoStdoutLeak(missingPath, "missing user data")) {
        return false;
    }

    return true;
}

bool runProcessCommand(
    const std::filesystem::path& backendExePath,
    const std::string& arguments,
    const std::filesystem::path& stdinPath,
    const std::filesystem::path& stdoutPath,
    const std::filesystem::path& stderrPath
) {
    std::string command = "\"" + quotePath(backendExePath) + " " + arguments;
    if (!stdinPath.empty()) {
        command += " < " + quotePath(stdinPath);
    }
    command += " > " + quotePath(stdoutPath) + " 2> " + quotePath(stderrPath);
    command += "\"";
    return std::system(command.c_str()) == 0;
}

bool runInvalidCliProcessTest(
    const std::filesystem::path& backendExePath,
    const std::string& arguments,
    const std::string& label
) {
    const auto base = std::filesystem::temp_directory_path() / ("tundraux_backend_stdio_" + label);
    const auto stdoutPath = base.string() + ".stdout.txt";
    const auto stderrPath = base.string() + ".stderr.txt";
    std::filesystem::remove(stdoutPath);
    std::filesystem::remove(stderrPath);

    const bool succeeded = runProcessCommand(backendExePath, arguments, {}, stdoutPath, stderrPath);
    const std::string stdoutText = readTextFile(stdoutPath);
    const std::string stderrText = readTextFile(stderrPath);
    std::filesystem::remove(stdoutPath);
    std::filesystem::remove(stderrPath);

    if (succeeded) {
        std::cerr << label << " should exit nonzero\n";
        return false;
    }
    if (!stdoutText.empty()) {
        std::cerr << label << " wrote stdout: " << stdoutText << "\n";
        return false;
    }
    if (stderrText.find("Usage: tundraux_backend_stdio") == std::string::npos) {
        std::cerr << label << " missing usage on stderr: " << stderrText << "\n";
        return false;
    }
    return true;
}

bool runMalformedStorageProcessTest(const std::filesystem::path& backendExePath) {
    const auto base = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_process_malformed";
    const auto userDataPath = base.string() + ".dat";
    const auto stdinPath = base.string() + ".stdin.txt";
    const auto stdoutPath = base.string() + ".stdout.txt";
    const auto stderrPath = base.string() + ".stderr.txt";
    {
        std::ofstream file(userDataPath, std::ios::binary | std::ios::trunc);
        file << "not a user data file";
    }
    {
        std::ofstream file(stdinPath, std::ios::binary | std::ios::trunc);
        file << R"({"id":"1","method":"session.startGuestSession","params":{}})" << "\n";
    }

    const bool succeeded = runProcessCommand(
        backendExePath,
        "--user-data " + quotePath(userDataPath),
        stdinPath,
        stdoutPath,
        stderrPath
    );
    const std::string stdoutText = readTextFile(stdoutPath);
    const std::string stderrText = readTextFile(stderrPath);
    std::filesystem::remove(userDataPath);
    std::filesystem::remove(stdinPath);
    std::filesystem::remove(stdoutPath);
    std::filesystem::remove(stderrPath);

    if (!succeeded) {
        std::cerr << "malformed storage process should exit zero\n";
        return false;
    }
    std::istringstream stdoutLines(stdoutText);
    std::string line;
    int jsonLineCount = 0;
    while (std::getline(stdoutLines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        ++jsonLineCount;
        if (line.front() != '{' || line.back() != '}') {
            std::cerr << "malformed storage process stdout contains non-JSON line: " << line << "\n";
            return false;
        }
    }

    if (jsonLineCount != 1) {
        std::cerr << "malformed storage process stdout should contain one JSON line: " << stdoutText << "\n";
        return false;
    }
    if (stdoutText.find(R"("sessionId")") == std::string::npos ||
        stdoutText.find(R"("type":"guest")") == std::string::npos) {
        std::cerr << "malformed storage process stdout missing guest response: " << stdoutText << "\n";
        return false;
    }
    if (stdoutText.find("Error:") != std::string::npos || stdoutText.find("\x1b[") != std::string::npos) {
        std::cerr << "malformed storage process stdout contains legacy diagnostics: " << stdoutText << "\n";
        return false;
    }
    if (stderrText.find("Error:") != std::string::npos || stderrText.find("\x1b[") != std::string::npos) {
        std::cerr << "malformed storage process stderr contains legacy diagnostics: " << stderrText << "\n";
        return false;
    }
    return true;
}

bool runGuestSessionProcessTest(const std::filesystem::path& backendExePath) {
    const auto base = std::filesystem::temp_directory_path() / "tundraux_backend_stdio_process_guest_session";
    const auto stdinPath = base.string() + ".stdin.txt";
    const auto stdoutPath = base.string() + ".stdout.txt";
    const auto stderrPath = base.string() + ".stderr.txt";
    {
        std::ofstream file(stdinPath, std::ios::binary | std::ios::trunc);
        file << R"({"id":"1","method":"session.startGuestSession","params":{}})" << "\n";
    }

    const bool succeeded = runProcessCommand(backendExePath, "", stdinPath, stdoutPath, stderrPath);
    std::string stdoutText = readTextFile(stdoutPath);
    const std::string stderrText = readTextFile(stderrPath);
    std::filesystem::remove(stdinPath);
    std::filesystem::remove(stdoutPath);
    std::filesystem::remove(stderrPath);

    if (!succeeded) {
        std::cerr << "guest session process should exit zero\n";
        return false;
    }
    if (!stderrText.empty()) {
        std::cerr << "guest session process wrote stderr: " << stderrText << "\n";
        return false;
    }
    if (!stdoutText.empty() && stdoutText.back() == '\n') {
        stdoutText.pop_back();
    }
    if (!stdoutText.empty() && stdoutText.back() == '\r') {
        stdoutText.pop_back();
    }

    const auto parsed = tundraux::backend::parseJson(stdoutText);
    if (!parsed.ok) {
        std::cerr << "guest session process stdout should parse: " << stdoutText << "\n";
        return false;
    }
    const auto& object = parsed.value.asObject();
    const auto& result = object.at("result").asObject();
    const auto& user = result.at("user").asObject();
    if (object.at("id").asString() != "1" ||
        result.at("sessionId").asString().empty() ||
        user.at("type").asString() != "guest" ||
        !user.at("name").asString().empty()) {
        std::cerr << "guest session process stdout has wrong protocol fields: " << stdoutText << "\n";
        return false;
    }
    return true;
}

bool runProcessTests(const std::filesystem::path& backendExePath) {
    if (backendExePath.empty() || !std::filesystem::exists(backendExePath)) {
        std::cerr << "backend stdio executable path is missing or does not exist: "
                  << backendExePath.string() << "\n";
        return false;
    }

    return runGuestSessionProcessTest(backendExePath) &&
        runInvalidCliProcessTest(backendExePath, "--bad", "unknown_arg") &&
        runInvalidCliProcessTest(backendExePath, "--user-data", "missing_user_data_value") &&
        runMalformedStorageProcessTest(backendExePath);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: backend_stdio_tests <tundraux_backend_stdio_exe>\n";
        return 1;
    }

    if (!runGuestSessionSmokeTest()) {
        return 1;
    }
    if (!runInvalidStorageInProcessTests()) {
        return 1;
    }
    if (!runProcessTests(argv[1])) {
        return 1;
    }

    return 0;
}

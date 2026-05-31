#include "data_manager_user_store.hpp"
#include "file_service.hpp"
#include "filesystem_file_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"
#include "crypto.hpp"

#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>

namespace {

bool runGuestSessionSmokeTest() {
    tundraux::backend::DataManagerUserStore store("user_data.dat");
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::FilesystemFileStore filesStore(
        (std::filesystem::temp_directory_path() / "tundraux_backend_stdio_smoke_files").string()
    );
    tundraux::backend::FileService files(filesStore, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users, files);

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

std::filesystem::path uniqueTempPath(const std::string& label) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("tundraux_backend_stdio_" + label + "_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(ticks));
}

void writeStoredString(std::ofstream& file, const std::string& value) {
    const std::size_t length = value.size();
    file.write(reinterpret_cast<const char*>(&length), sizeof(length));
    file.write(value.data(), static_cast<std::streamsize>(length));
}

bool writeUserDataFile(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    const int version = 21;
    const std::uint8_t strictMode = 0;
    const std::size_t userCount = 1;
    const int failedCount = 0;

    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&strictMode), sizeof(strictMode));
    file.write(reinterpret_cast<const char*>(&userCount), sizeof(userCount));
    writeStoredString(file, "admin");
    writeStoredString(file, "alice");
    writeStoredString(file, encrypt("Secret1"));
    writeStoredString(file, "hint");
    file.write(reinterpret_cast<const char*>(&failedCount), sizeof(failedCount));
    return static_cast<bool>(file);
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
    const auto filesRoot = uniqueTempPath(label + "_files");
    tundraux::backend::FilesystemFileStore filesStore(filesRoot.string());
    tundraux::backend::FileService files(filesStore, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users, files);
    const std::string sessionId = startGuestSession(dispatcher);
    if (sessionId.empty()) {
        std::cerr << label << " could not start guest session\n";
        std::filesystem::remove_all(filesRoot);
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
        std::filesystem::remove_all(filesRoot);
        return false;
    }
    if (response.find(R"("code":"StorageError")") == std::string::npos) {
        std::cerr << label << " response missing StorageError: " << response << "\n";
        std::filesystem::remove_all(filesRoot);
        return false;
    }
    std::filesystem::remove_all(filesRoot);
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

struct InteractiveProcess {
    HANDLE process = INVALID_HANDLE_VALUE;
    HANDLE thread = INVALID_HANDLE_VALUE;
    HANDLE stdinWrite = INVALID_HANDLE_VALUE;
    HANDLE stdoutRead = INVALID_HANDLE_VALUE;
    HANDLE stderrRead = INVALID_HANDLE_VALUE;
};

void closeHandle(HANDLE& handle) {
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

void cleanupInteractiveProcess(InteractiveProcess& child) {
    closeHandle(child.stdinWrite);
    if (child.process != INVALID_HANDLE_VALUE && child.process != nullptr) {
        const DWORD waitResult = WaitForSingleObject(child.process, 1000);
        if (waitResult != WAIT_OBJECT_0) {
            TerminateProcess(child.process, 1);
            WaitForSingleObject(child.process, 1000);
        }
    }
    closeHandle(child.stdoutRead);
    closeHandle(child.stderrRead);
    closeHandle(child.thread);
    closeHandle(child.process);
}

std::string quoteArg(const std::string& arg) {
    std::string quoted = "\"";
    for (const char ch : arg) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

bool startInteractiveProcess(
    const std::filesystem::path& backendExePath,
    const std::vector<std::string>& args,
    InteractiveProcess& child
) {
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdinRead = INVALID_HANDLE_VALUE;
    HANDLE stdoutWrite = INVALID_HANDLE_VALUE;
    HANDLE stderrWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&stdinRead, &child.stdinWrite, &securityAttributes, 0) ||
        !CreatePipe(&child.stdoutRead, &stdoutWrite, &securityAttributes, 0) ||
        !CreatePipe(&child.stderrRead, &stderrWrite, &securityAttributes, 0)) {
        closeHandle(stdinRead);
        closeHandle(child.stdinWrite);
        closeHandle(child.stdoutRead);
        closeHandle(stdoutWrite);
        closeHandle(child.stderrRead);
        closeHandle(stderrWrite);
        return false;
    }

    SetHandleInformation(child.stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child.stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child.stderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead;
    startupInfo.hStdOutput = stdoutWrite;
    startupInfo.hStdError = stderrWrite;

    PROCESS_INFORMATION processInfo{};
    std::string commandLine = quoteArg(backendExePath.string());
    for (const auto& arg : args) {
        commandLine += " " + quoteArg(arg);
    }
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    const BOOL created = CreateProcessA(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo
    );

    closeHandle(stdinRead);
    closeHandle(stdoutWrite);
    closeHandle(stderrWrite);

    if (!created) {
        closeHandle(child.stdinWrite);
        closeHandle(child.stdoutRead);
        closeHandle(child.stderrRead);
        return false;
    }

    child.process = processInfo.hProcess;
    child.thread = processInfo.hThread;
    return true;
}

bool writeProcessLine(InteractiveProcess& child, const std::string& line) {
    const std::string withNewline = line + "\n";
    DWORD written = 0;
    return WriteFile(
        child.stdinWrite,
        withNewline.data(),
        static_cast<DWORD>(withNewline.size()),
        &written,
        nullptr
    ) && written == withNewline.size();
}

bool readProcessLine(InteractiveProcess& child, std::string& line) {
    line.clear();
    char ch = '\0';
    DWORD bytesRead = 0;
    while (ReadFile(child.stdoutRead, &ch, 1, &bytesRead, nullptr) && bytesRead == 1) {
        if (ch == '\n') {
            return true;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }
    return false;
}

std::string readPipeToEnd(HANDLE pipe) {
    std::string output;
    char buffer[256];
    DWORD bytesRead = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer, buffer + bytesRead);
    }
    return output;
}

bool stopInteractiveProcess(InteractiveProcess& child, DWORD& exitCode, std::string& remainingStdout, std::string& stderrText) {
    closeHandle(child.stdinWrite);
    const DWORD waitResult = WaitForSingleObject(child.process, 5000);
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(child.process, 1);
        cleanupInteractiveProcess(child);
        return false;
    }

    remainingStdout = readPipeToEnd(child.stdoutRead);
    stderrText = readPipeToEnd(child.stderrRead);
    if (!GetExitCodeProcess(child.process, &exitCode)) {
        cleanupInteractiveProcess(child);
        return false;
    }

    closeHandle(child.stdoutRead);
    closeHandle(child.stderrRead);
    closeHandle(child.thread);
    closeHandle(child.process);
    return true;
}

bool expectJsonOnlyLine(const std::string& line, const std::string& label) {
    if (line.empty() || line.front() != '{' || line.back() != '}') {
        std::cerr << label << " stdout contains non-JSON line: " << line << "\n";
        return false;
    }
    const auto parsed = tundraux::backend::parseJson(line);
    if (!parsed.ok) {
        std::cerr << label << " stdout line did not parse as JSON: " << line << "\n";
        return false;
    }
    return true;
}

bool sendRequestAndReadResponse(
    InteractiveProcess& child,
    const std::string& request,
    const std::string& label,
    std::string& response
) {
    if (!writeProcessLine(child, request)) {
        std::cerr << label << " failed to write request\n";
        return false;
    }
    if (!readProcessLine(child, response)) {
        std::cerr << label << " failed to read response\n";
        return false;
    }
    return expectJsonOnlyLine(response, label);
}

bool stderrHasLegacyColorOutput(const std::string& text) {
    return text.find("Error:") != std::string::npos || text.find("\x1b[") != std::string::npos;
}

bool runFileApiProcessTest(const std::filesystem::path& backendExePath) {
    const auto base = uniqueTempPath("process_file_api");
    const std::filesystem::path userDataPath = base.string() + "_user_data.dat";
    const std::filesystem::path filesRoot = base.string() + "_files";
    std::filesystem::remove(userDataPath);
    std::filesystem::remove_all(filesRoot);

    InteractiveProcess child;
    const auto cleanupFiles = [&]() {
        std::filesystem::remove(userDataPath);
        std::filesystem::remove(userDataPath.string() + ".tmp");
        std::filesystem::remove_all(filesRoot);
    };
    const auto fail = [&]() {
        cleanupInteractiveProcess(child);
        cleanupFiles();
        return false;
    };

    if (!writeUserDataFile(userDataPath)) {
        std::cerr << "file api process failed to write user data\n";
        return fail();
    }
    std::filesystem::create_directories(filesRoot);

    if (!startInteractiveProcess(
            backendExePath,
            {"--user-data", userDataPath.string(), "--files-root", filesRoot.string()},
            child)) {
        std::cerr << "file api process failed to start\n";
        return fail();
    }

    std::string response;
    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"1","method":"session.startGuestSession","params":{}})",
            "file api guest session",
            response)) {
        return fail();
    }
    const auto guest = tundraux::backend::parseJson(response);
    const std::string sessionId = guest.value.asObject().at("result").asObject().at("sessionId").asString();
    if (sessionId.empty()) {
        std::cerr << "file api process returned empty session id\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
                R"(","username":"alice","password":"Secret1"}})",
            "file api login",
            response)) {
        return fail();
    }
    const auto login = tundraux::backend::parseJson(response);
    if (login.value.asObject().find("error") != login.value.asObject().end()) {
        std::cerr << "file api login returned error: " << response << "\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"3","method":"file.writeFile","params":{"sessionId":")" + sessionId +
                R"(","path":"note.txt","content":"hello"}})",
            "file api write",
            response)) {
        return fail();
    }
    const auto write = tundraux::backend::parseJson(response);
    if (!write.value.asObject().at("result").asObject().at("ok").asBoolean()) {
        std::cerr << "file api write did not return ok: " << response << "\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"4","method":"file.readFile","params":{"sessionId":")" + sessionId +
                R"(","path":"note.txt"}})",
            "file api read",
            response)) {
        return fail();
    }
    const auto read = tundraux::backend::parseJson(response);
    if (read.value.asObject().at("result").asObject().at("content").asString() != "hello") {
        std::cerr << "file api read content mismatch: " << response << "\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"5","method":"file.listDirectory","params":{"sessionId":")" + sessionId +
                R"(","path":""}})",
            "file api list",
            response)) {
        return fail();
    }
    const auto list = tundraux::backend::parseJson(response);
    const auto& entries = list.value.asObject().at("result").asObject().at("entries").asArray();
    bool foundNote = false;
    for (const auto& entryValue : entries) {
        const auto& entry = entryValue.asObject();
        if (entry.at("name").asString() == "note.txt" &&
            entry.at("path").asString() == "note.txt" &&
            entry.at("type").asString() == "file") {
            foundNote = true;
        }
    }
    if (!foundNote) {
        std::cerr << "file api list missing note.txt entry: " << response << "\n";
        return fail();
    }

    DWORD exitCode = 1;
    std::string remainingStdout;
    std::string stderrText;
    if (!stopInteractiveProcess(child, exitCode, remainingStdout, stderrText)) {
        std::cerr << "file api process failed to stop\n";
        cleanupFiles();
        return false;
    }

    cleanupFiles();

    if (exitCode != 0) {
        std::cerr << "file api process exit code mismatch: " << exitCode << "\n";
        return false;
    }
    if (!remainingStdout.empty()) {
        std::cerr << "file api process wrote unexpected trailing stdout: " << remainingStdout << "\n";
        return false;
    }
    if (stderrHasLegacyColorOutput(stderrText)) {
        std::cerr << "file api process stderr contains legacy diagnostics: " << stderrText << "\n";
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
        runInvalidCliProcessTest(backendExePath, "--files-root", "missing_files_root_value") &&
        runFileApiProcessTest(backendExePath) &&
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

#include "data_manager_user_store.hpp"
#include "file_service.hpp"
#include "filesystem_file_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"
#include "crypto.hpp"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
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

class ScopedPathCleanup {
public:
    explicit ScopedPathCleanup(std::initializer_list<std::filesystem::path> paths) : paths_(paths) {}
    ~ScopedPathCleanup() {
        cleanup();
    }

    void cleanup() {
        for (const auto& path : paths_) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }

private:
    std::vector<std::filesystem::path> paths_;
};

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
    const auto malformedPath = uniqueTempPath("malformed_user_data").string() + ".dat";
    ScopedPathCleanup malformedCleanup({malformedPath});
    {
        std::ofstream file(malformedPath, std::ios::binary | std::ios::trunc);
        file << "not a user data file";
    }
    if (!responseHasStorageErrorWithNoStdoutLeak(malformedPath, "malformed user data")) {
        return false;
    }
    malformedCleanup.cleanup();

    const auto emptyPath = uniqueTempPath("empty_user_data").string() + ".dat";
    ScopedPathCleanup emptyCleanup({emptyPath});
    {
        std::ofstream file(emptyPath, std::ios::binary | std::ios::trunc);
    }
    if (!responseHasStorageErrorWithNoStdoutLeak(emptyPath, "empty user data")) {
        return false;
    }
    emptyCleanup.cleanup();

    const auto directoryPath = uniqueTempPath("user_data_dir");
    ScopedPathCleanup directoryCleanup({directoryPath});
    std::filesystem::remove_all(directoryPath);
    std::filesystem::create_directory(directoryPath);
    if (!responseHasStorageErrorWithNoStdoutLeak(directoryPath, "directory user data path")) {
        return false;
    }
    directoryCleanup.cleanup();

    const auto missingPath = uniqueTempPath("missing_user_data").string() + ".dat";
    ScopedPathCleanup missingCleanup({missingPath});
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
    const auto base = uniqueTempPath("process_" + label);
    const auto stdoutPath = base.string() + ".stdout.txt";
    const auto stderrPath = base.string() + ".stderr.txt";
    ScopedPathCleanup cleanup({stdoutPath, stderrPath});
    std::filesystem::remove(stdoutPath);
    std::filesystem::remove(stderrPath);

    const bool succeeded = runProcessCommand(backendExePath, arguments, {}, stdoutPath, stderrPath);
    const std::string stdoutText = readTextFile(stdoutPath);
    const std::string stderrText = readTextFile(stderrPath);
    cleanup.cleanup();

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
    std::string stdoutText;
    std::string stderrText;
    std::string stdoutLineBuffer;
    std::deque<std::string> stdoutLines;
    std::string diagnostics;
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

std::string windowsErrorMessage(DWORD error) {
    if (error == ERROR_SUCCESS) {
        return "success";
    }

    LPSTR messageBuffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&messageBuffer),
        0,
        nullptr
    );
    std::string message = length > 0 && messageBuffer != nullptr
        ? std::string(messageBuffer, length)
        : "unknown error";
    if (messageBuffer != nullptr) {
        LocalFree(messageBuffer);
    }
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) {
        message.pop_back();
    }
    return message + " (" + std::to_string(error) + ")";
}

std::string quoteArg(const std::string& arg) {
    std::string quoted = "\"";
    std::size_t backslashCount = 0;
    for (const char ch : arg) {
        if (ch == '\\') {
            ++backslashCount;
            continue;
        }
        if (ch == '"') {
            quoted.append(backslashCount * 2 + 1, '\\');
            quoted += '"';
            backslashCount = 0;
            continue;
        }
        quoted.append(backslashCount, '\\');
        backslashCount = 0;
        quoted += ch;
    }
    quoted.append(backslashCount * 2, '\\');
    quoted += "\"";
    return quoted;
}

bool setStartupError(InteractiveProcess& child, const std::string& context, DWORD error = GetLastError()) {
    child.diagnostics = context + ": " + windowsErrorMessage(error);
    return false;
}

bool startInteractiveProcess(
    const std::filesystem::path& backendExePath,
    const std::vector<std::string>& args,
    InteractiveProcess& child
) {
    child = InteractiveProcess{};
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdinRead = INVALID_HANDLE_VALUE;
    HANDLE stdoutWrite = INVALID_HANDLE_VALUE;
    HANDLE stderrWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&stdinRead, &child.stdinWrite, &securityAttributes, 0)) {
        setStartupError(child, "CreatePipe(stdin) failed");
        closeHandle(stdinRead);
        closeHandle(child.stdinWrite);
        return false;
    }
    if (!CreatePipe(&child.stdoutRead, &stdoutWrite, &securityAttributes, 0)) {
        setStartupError(child, "CreatePipe(stdout) failed");
        closeHandle(stdinRead);
        closeHandle(child.stdinWrite);
        closeHandle(child.stdoutRead);
        closeHandle(stdoutWrite);
        return false;
    }
    if (!CreatePipe(&child.stderrRead, &stderrWrite, &securityAttributes, 0)) {
        setStartupError(child, "CreatePipe(stderr) failed");
        closeHandle(stdinRead);
        closeHandle(child.stdinWrite);
        closeHandle(child.stdoutRead);
        closeHandle(stdoutWrite);
        closeHandle(child.stderrRead);
        closeHandle(stderrWrite);
        return false;
    }

    // This helper keeps STARTUPINFOA instead of STARTUPINFOEX handle lists. That
    // means any unrelated parent handle that is already inheritable can still be
    // inherited, but the pipe handles created here only leave the child ends
    // inheritable and clear inheritance on all parent-side pipe handles.
    if (!SetHandleInformation(child.stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        setStartupError(child, "SetHandleInformation(stdin write) failed");
        closeHandle(stdinRead);
        closeHandle(child.stdinWrite);
        closeHandle(child.stdoutRead);
        closeHandle(stdoutWrite);
        closeHandle(child.stderrRead);
        closeHandle(stderrWrite);
        return false;
    }
    if (!SetHandleInformation(child.stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        setStartupError(child, "SetHandleInformation(stdout read) failed");
        closeHandle(stdinRead);
        closeHandle(child.stdinWrite);
        closeHandle(child.stdoutRead);
        closeHandle(stdoutWrite);
        closeHandle(child.stderrRead);
        closeHandle(stderrWrite);
        return false;
    }
    if (!SetHandleInformation(child.stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        setStartupError(child, "SetHandleInformation(stderr read) failed");
        closeHandle(stdinRead);
        closeHandle(child.stdinWrite);
        closeHandle(child.stdoutRead);
        closeHandle(stdoutWrite);
        closeHandle(child.stderrRead);
        closeHandle(stderrWrite);
        return false;
    }

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
    const DWORD createProcessError = GetLastError();

    closeHandle(stdinRead);
    closeHandle(stdoutWrite);
    closeHandle(stderrWrite);

    if (!created) {
        setStartupError(child, "CreateProcessA failed", createProcessError);
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
    const BOOL wrote = WriteFile(
        child.stdinWrite,
        withNewline.data(),
        static_cast<DWORD>(withNewline.size()),
        &written,
        nullptr
    );
    if (!wrote) {
        child.diagnostics = "WriteFile(stdin) failed: " + windowsErrorMessage(GetLastError());
        return false;
    }
    if (written != withNewline.size()) {
        child.diagnostics = "WriteFile(stdin) wrote " + std::to_string(written) +
            " of " + std::to_string(withNewline.size()) + " bytes";
        return false;
    }
    return true;
}

bool readAvailablePipe(HANDLE pipe, std::string& captured, std::deque<std::string>* lines, std::string* lineBuffer) {
    if (pipe == INVALID_HANDLE_VALUE || pipe == nullptr) {
        return true;
    }

    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD error = GetLastError();
            return error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF;
        }
        if (available == 0) {
            return true;
        }

        char buffer[512];
        const DWORD toRead = std::min<DWORD>(available, sizeof(buffer));
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, buffer, toRead, &bytesRead, nullptr) || bytesRead == 0) {
            const DWORD error = GetLastError();
            return error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF;
        }

        captured.append(buffer, buffer + bytesRead);
        if (lines != nullptr && lineBuffer != nullptr) {
            for (DWORD index = 0; index < bytesRead; ++index) {
                const char ch = buffer[index];
                if (ch == '\n') {
                    lines->push_back(*lineBuffer);
                    lineBuffer->clear();
                } else if (ch != '\r') {
                    lineBuffer->push_back(ch);
                }
            }
        }
    }
}

void drainAvailableProcessOutput(InteractiveProcess& child) {
    readAvailablePipe(child.stdoutRead, child.stdoutText, &child.stdoutLines, &child.stdoutLineBuffer);
    readAvailablePipe(child.stderrRead, child.stderrText, nullptr, nullptr);
}

void readPipeToEndInto(HANDLE pipe, std::string& captured, std::deque<std::string>* lines, std::string* lineBuffer) {
    if (pipe == INVALID_HANDLE_VALUE || pipe == nullptr) {
        return;
    }

    char buffer[512];
    DWORD bytesRead = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        captured.append(buffer, buffer + bytesRead);
        if (lines != nullptr && lineBuffer != nullptr) {
            for (DWORD index = 0; index < bytesRead; ++index) {
                const char ch = buffer[index];
                if (ch == '\n') {
                    lines->push_back(*lineBuffer);
                    lineBuffer->clear();
                } else if (ch != '\r') {
                    lineBuffer->push_back(ch);
                }
            }
        }
    }
}

void drainProcessOutputToEnd(InteractiveProcess& child) {
    readPipeToEndInto(child.stdoutRead, child.stdoutText, &child.stdoutLines, &child.stdoutLineBuffer);
    readPipeToEndInto(child.stderrRead, child.stderrText, nullptr, nullptr);
}

void terminateInteractiveProcess(InteractiveProcess& child, const std::string& reason) {
    child.diagnostics = reason;
    closeHandle(child.stdinWrite);
    if (child.process != INVALID_HANDLE_VALUE && child.process != nullptr) {
        DWORD exitCode = STILL_ACTIVE;
        if (GetExitCodeProcess(child.process, &exitCode) && exitCode == STILL_ACTIVE) {
            if (!TerminateProcess(child.process, 1)) {
                child.diagnostics += "; TerminateProcess failed: " + windowsErrorMessage(GetLastError());
            }
        }
        const DWORD waitResult = WaitForSingleObject(child.process, 1000);
        if (waitResult == WAIT_OBJECT_0) {
            drainProcessOutputToEnd(child);
        } else {
            child.diagnostics += "; child did not exit after terminate";
            drainAvailableProcessOutput(child);
        }
    }
}

std::string takePendingStdout(InteractiveProcess& child) {
    std::string output;
    while (!child.stdoutLines.empty()) {
        output += child.stdoutLines.front();
        output += "\n";
        child.stdoutLines.pop_front();
    }
    if (!child.stdoutLineBuffer.empty()) {
        output += child.stdoutLineBuffer;
        child.stdoutLineBuffer.clear();
    }
    return output;
}

bool readProcessLine(InteractiveProcess& child, std::string& line, DWORD timeoutMs = 5000) {
    line.clear();
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        drainAvailableProcessOutput(child);
        if (!child.stdoutLines.empty()) {
            line = child.stdoutLines.front();
            child.stdoutLines.pop_front();
            return true;
        }

        const DWORD waitResult = WaitForSingleObject(child.process, 25);
        if (waitResult == WAIT_OBJECT_0) {
            drainProcessOutputToEnd(child);
            if (!child.stdoutLines.empty()) {
                line = child.stdoutLines.front();
                child.stdoutLines.pop_front();
                return true;
            }
            child.diagnostics = "child exited before writing a stdout response";
            return false;
        }
        if (waitResult == WAIT_FAILED) {
            child.diagnostics = "WaitForSingleObject(process) failed: " + windowsErrorMessage(GetLastError());
            return false;
        }

        if (GetTickCount64() >= deadline) {
            terminateInteractiveProcess(
                child,
                "timed out waiting " + std::to_string(timeoutMs) + " ms for a stdout response"
            );
            return false;
        }
    }
}

bool stopInteractiveProcess(InteractiveProcess& child, DWORD& exitCode, std::string& remainingStdout, std::string& stderrText) {
    closeHandle(child.stdinWrite);
    const ULONGLONG deadline = GetTickCount64() + 5000;
    for (;;) {
        drainAvailableProcessOutput(child);
        const DWORD waitResult = WaitForSingleObject(child.process, 25);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult == WAIT_FAILED) {
            child.diagnostics = "WaitForSingleObject(process) failed: " + windowsErrorMessage(GetLastError());
            stderrText = child.stderrText;
            remainingStdout = takePendingStdout(child);
            cleanupInteractiveProcess(child);
            return false;
        }
        if (GetTickCount64() >= deadline) {
            terminateInteractiveProcess(child, "timed out waiting 5000 ms for process exit");
            stderrText = child.stderrText;
            remainingStdout = takePendingStdout(child);
            cleanupInteractiveProcess(child);
            return false;
        }
    }

    drainProcessOutputToEnd(child);
    remainingStdout = takePendingStdout(child);
    stderrText = child.stderrText;
    if (!GetExitCodeProcess(child.process, &exitCode)) {
        child.diagnostics = "GetExitCodeProcess failed: " + windowsErrorMessage(GetLastError());
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
        const std::string writeDiagnostics = child.diagnostics;
        terminateInteractiveProcess(child, label + " failed to write request: " + writeDiagnostics);
        std::cerr << label << " failed to write request: " << child.diagnostics
                  << "\nstdout:\n" << child.stdoutText
                  << "\nstderr:\n" << child.stderrText << "\n";
        return false;
    }
    if (!readProcessLine(child, response)) {
        std::cerr << label << " failed to read response: " << child.diagnostics
                  << "\nstdout:\n" << child.stdoutText
                  << "\nstderr:\n" << child.stderrText << "\n";
        return false;
    }
    if (!expectJsonOnlyLine(response, label)) {
        std::cerr << label << " captured stdout:\n" << child.stdoutText
                  << "\nstderr:\n" << child.stderrText << "\n";
        return false;
    }
    return true;
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
        std::cerr << "file api process failed to start: " << child.diagnostics << "\n";
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
        std::cerr << "file api process failed to stop: " << child.diagnostics
                  << "\nstdout:\n" << child.stdoutText
                  << "\nstderr:\n" << child.stderrText << "\n";
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
    if (!stderrText.empty()) {
        std::cerr << "file api process wrote stderr: " << stderrText << "\n";
        return false;
    }
    return true;
}

bool runMissingFilesRootProcessTest(const std::filesystem::path& backendExePath) {
    const auto base = uniqueTempPath("process_missing_files_root");
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
        std::cerr << "missing files root process failed to write user data\n";
        return fail();
    }

    if (!startInteractiveProcess(
            backendExePath,
            {"--user-data", userDataPath.string(), "--files-root", filesRoot.string()},
            child)) {
        std::cerr << "missing files root process failed to start: " << child.diagnostics << "\n";
        return fail();
    }

    std::string response;
    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"1","method":"session.startGuestSession","params":{}})",
            "missing files root guest session",
            response)) {
        return fail();
    }
    const auto guest = tundraux::backend::parseJson(response);
    const std::string sessionId = guest.value.asObject().at("result").asObject().at("sessionId").asString();
    if (sessionId.empty()) {
        std::cerr << "missing files root process returned empty session id\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
                R"(","username":"alice","password":"Secret1"}})",
            "missing files root login",
            response)) {
        return fail();
    }
    const auto login = tundraux::backend::parseJson(response);
    if (login.value.asObject().find("error") != login.value.asObject().end()) {
        std::cerr << "missing files root login returned error: " << response << "\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"3","method":"file.listDirectory","params":{"sessionId":")" + sessionId +
                R"(","path":""}})",
            "missing files root initial list",
            response)) {
        return fail();
    }
    const auto initialList = tundraux::backend::parseJson(response);
    const auto& initialListObject = initialList.value.asObject();
    if (initialListObject.find("error") != initialListObject.end()) {
        const auto& error = initialListObject.at("error").asObject();
        if (error.at("code").asString() != "NotFound") {
            std::cerr << "missing files root initial list returned wrong error: " << response << "\n";
            return fail();
        }
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"4","method":"file.writeFile","params":{"sessionId":")" + sessionId +
                R"(","path":"note.txt","content":"hello missing root"}})",
            "missing files root write",
            response)) {
        return fail();
    }
    const auto write = tundraux::backend::parseJson(response);
    if (!write.value.asObject().at("result").asObject().at("ok").asBoolean()) {
        std::cerr << "missing files root write did not return ok: " << response << "\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"5","method":"file.readFile","params":{"sessionId":")" + sessionId +
                R"(","path":"note.txt"}})",
            "missing files root read",
            response)) {
        return fail();
    }
    const auto read = tundraux::backend::parseJson(response);
    if (read.value.asObject().at("result").asObject().at("content").asString() != "hello missing root") {
        std::cerr << "missing files root read content mismatch: " << response << "\n";
        return fail();
    }

    if (!sendRequestAndReadResponse(
            child,
            R"({"id":"6","method":"file.listDirectory","params":{"sessionId":")" + sessionId +
                R"(","path":""}})",
            "missing files root list after write",
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
        std::cerr << "missing files root list missing note.txt entry: " << response << "\n";
        return fail();
    }

    DWORD exitCode = 1;
    std::string remainingStdout;
    std::string stderrText;
    if (!stopInteractiveProcess(child, exitCode, remainingStdout, stderrText)) {
        std::cerr << "missing files root process failed to stop: " << child.diagnostics
                  << "\nstdout:\n" << child.stdoutText
                  << "\nstderr:\n" << child.stderrText << "\n";
        cleanupFiles();
        return false;
    }

    cleanupFiles();

    if (exitCode != 0) {
        std::cerr << "missing files root process exit code mismatch: " << exitCode << "\n";
        return false;
    }
    if (!remainingStdout.empty()) {
        std::cerr << "missing files root process wrote unexpected trailing stdout: " << remainingStdout << "\n";
        return false;
    }
    if (!stderrText.empty()) {
        std::cerr << "missing files root process wrote stderr: " << stderrText << "\n";
        return false;
    }
    return true;
}

bool runMalformedStorageProcessTest(const std::filesystem::path& backendExePath) {
    const auto base = uniqueTempPath("process_malformed");
    const auto userDataPath = base.string() + ".dat";
    const auto stdinPath = base.string() + ".stdin.txt";
    const auto stdoutPath = base.string() + ".stdout.txt";
    const auto stderrPath = base.string() + ".stderr.txt";
    ScopedPathCleanup cleanup({userDataPath, stdinPath, stdoutPath, stderrPath});
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
    cleanup.cleanup();

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
    const auto base = uniqueTempPath("process_guest_session");
    const auto stdinPath = base.string() + ".stdin.txt";
    const auto stdoutPath = base.string() + ".stdout.txt";
    const auto stderrPath = base.string() + ".stderr.txt";
    ScopedPathCleanup cleanup({stdinPath, stdoutPath, stderrPath});
    {
        std::ofstream file(stdinPath, std::ios::binary | std::ios::trunc);
        file << R"({"id":"1","method":"session.startGuestSession","params":{}})" << "\n";
    }

    const bool succeeded = runProcessCommand(backendExePath, "", stdinPath, stdoutPath, stderrPath);
    std::string stdoutText = readTextFile(stdoutPath);
    const std::string stderrText = readTextFile(stderrPath);
    cleanup.cleanup();

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
        runMissingFilesRootProcessTest(backendExePath) &&
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

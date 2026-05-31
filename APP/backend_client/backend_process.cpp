#include "backend_process.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

namespace tundraux::frontend {
namespace {

bool validHandle(HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

void closeHandle(HANDLE& handle) {
    if (validHandle(handle)) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

std::string quoteWindowsArg(const std::string& arg) {
    if (arg.empty()) {
        return "\"\"";
    }

    const bool needsQuotes = arg.find_first_of(" \t\n\v\"") != std::string::npos;
    if (!needsQuotes) {
        return arg;
    }

    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }

        if (ch == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back(ch);
        } else {
            quoted.append(backslashes, '\\');
            quoted.push_back(ch);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

bool writeAll(HANDLE handle, const std::string& data) {
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const DWORD chunk = remaining > static_cast<std::size_t>(MAXDWORD)
            ? MAXDWORD
            : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(handle, cursor, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

} // namespace

BackendProcessTransport::~BackendProcessTransport() {
    stop();
}

bool BackendProcessTransport::start(
    const std::string& executablePath,
    const std::string& userDataPath,
    const std::string& filesRoot
) {
    stop();

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;

    if (!CreatePipe(&stdinRead, &stdinWrite, &securityAttributes, 0)) {
        return false;
    }
    if (!SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        closeHandle(stdinRead);
        closeHandle(stdinWrite);
        return false;
    }

    if (!CreatePipe(&stdoutRead, &stdoutWrite, &securityAttributes, 0)) {
        closeHandle(stdinRead);
        closeHandle(stdinWrite);
        return false;
    }
    if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        closeHandle(stdinRead);
        closeHandle(stdinWrite);
        closeHandle(stdoutRead);
        closeHandle(stdoutWrite);
        return false;
    }

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead;
    startupInfo.hStdOutput = stdoutWrite;
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (startupInfo.hStdError == INVALID_HANDLE_VALUE) {
        startupInfo.hStdError = nullptr;
    }

    PROCESS_INFORMATION processInfo{};
    const std::string commandLine =
        quoteWindowsArg(executablePath) +
        " --user-data " + quoteWindowsArg(userDataPath) +
        " --files-root " + quoteWindowsArg(filesRoot);
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    const BOOL created = CreateProcessA(
        executablePath.c_str(),
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo
    );

    closeHandle(stdinRead);
    closeHandle(stdoutWrite);

    if (!created) {
        closeHandle(stdinWrite);
        closeHandle(stdoutRead);
        return false;
    }

    processInfo_ = processInfo;
    childStdinWrite_ = stdinWrite;
    childStdoutRead_ = stdoutRead;
    return true;
}

bool BackendProcessTransport::requestLine(const std::string& line, std::string& response) {
    response.clear();
    if (!validHandle(childStdinWrite_) || !validHandle(childStdoutRead_) || !validHandle(processInfo_.hProcess)) {
        return false;
    }

    if (!writeAll(childStdinWrite_, line + "\n")) {
        stop();
        return false;
    }

    while (true) {
        char ch = '\0';
        DWORD read = 0;
        if (!ReadFile(childStdoutRead_, &ch, 1, &read, nullptr) || read == 0) {
            stop();
            return false;
        }
        if (ch == '\n') {
            return true;
        }
        if (ch != '\r') {
            response.push_back(ch);
        }
    }
}

void BackendProcessTransport::stop() {
    closeHandle(childStdinWrite_);

    if (validHandle(processInfo_.hProcess)) {
        WaitForSingleObject(processInfo_.hProcess, INFINITE);
    }

    closeHandle(childStdoutRead_);
    closeHandle(processInfo_.hThread);
    closeHandle(processInfo_.hProcess);
    std::memset(&processInfo_, 0, sizeof(processInfo_));
}

} // namespace tundraux::frontend

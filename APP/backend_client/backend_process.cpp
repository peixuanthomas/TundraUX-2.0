#include "backend_process.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace tundraux::frontend {
namespace {

constexpr DWORD kRequestDeadlineMs = 30000;
constexpr DWORD kStopGraceMs = 5000;
constexpr DWORD kPostTerminateWaitMs = 1000;
constexpr std::size_t kMaxResponseLineBytes = 20ULL * 1024ULL * 1024ULL;
constexpr DWORD kPipeBufferBytes = 64 * 1024;

bool validHandle(HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

void closeHandle(HANDLE& handle) {
    if (validHandle(handle)) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

std::wstring widenString(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (length <= 0) {
        codePage = CP_ACP;
        flags = 0;
        length = MultiByteToWideChar(
            codePage,
            flags,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0
        );
    }
    if (length <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        codePage,
        flags,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length
    );
    return result;
}

std::wstring quoteWindowsArg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes = arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return arg;
    }

    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }

        if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(ch);
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(ch);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

DWORD remainingMillis(ULONGLONG deadline) {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) {
        return 0;
    }
    const ULONGLONG remaining = deadline - now;
    return remaining > MAXDWORD ? MAXDWORD : static_cast<DWORD>(remaining);
}

std::wstring uniquePipeName() {
    static std::atomic<unsigned long> counter{0};
    return L"\\\\.\\pipe\\tundraux_backend_stdio_" +
        std::to_wstring(GetCurrentProcessId()) + L"_" +
        std::to_wstring(GetTickCount64()) + L"_" +
        std::to_wstring(++counter);
}

bool createOverlappedPipePair(bool parentWrites, HANDLE& parentHandle, HANDLE& childHandle) {
    parentHandle = nullptr;
    childHandle = nullptr;

    const std::wstring pipeName = uniquePipeName();
    const DWORD pipeAccess = (parentWrites ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND) |
        FILE_FLAG_OVERLAPPED;
    parentHandle = CreateNamedPipeW(
        pipeName.c_str(),
        pipeAccess,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        kPipeBufferBytes,
        kPipeBufferBytes,
        0,
        nullptr
    );
    if (!validHandle(parentHandle)) {
        parentHandle = nullptr;
        return false;
    }
    SetHandleInformation(parentHandle, HANDLE_FLAG_INHERIT, 0);

    SECURITY_ATTRIBUTES childSecurity{};
    childSecurity.nLength = sizeof(childSecurity);
    childSecurity.bInheritHandle = TRUE;

    const DWORD childAccess = parentWrites ? GENERIC_READ : GENERIC_WRITE;
    childHandle = CreateFileW(
        pipeName.c_str(),
        childAccess,
        0,
        &childSecurity,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (!validHandle(childHandle)) {
        childHandle = nullptr;
        closeHandle(parentHandle);
        return false;
    }

    if (!ConnectNamedPipe(parentHandle, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
        closeHandle(childHandle);
        closeHandle(parentHandle);
        return false;
    }
    return true;
}

HANDLE createChildStderrHandle() {
    SECURITY_ATTRIBUTES childSecurity{};
    childSecurity.nLength = sizeof(childSecurity);
    childSecurity.bInheritHandle = TRUE;

    const HANDLE childStderr = CreateFileW(
        L"NUL",
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &childSecurity,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    return validHandle(childStderr) ? childStderr : nullptr;
}

void cancelOverlappedAndDrain(HANDLE handle, OVERLAPPED& overlapped) {
    CancelIoEx(handle, &overlapped);

    const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, INFINITE);
    if (waitResult == WAIT_OBJECT_0) {
        DWORD ignored = 0;
        GetOverlappedResult(handle, &overlapped, &ignored, FALSE);
    }
}

bool waitForOverlapped(HANDLE handle, OVERLAPPED& overlapped, DWORD& transferred, ULONGLONG deadline) {
    const DWORD waitMs = remainingMillis(deadline);
    if (waitMs == 0) {
        cancelOverlappedAndDrain(handle, overlapped);
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, waitMs);
    if (waitResult == WAIT_TIMEOUT) {
        cancelOverlappedAndDrain(handle, overlapped);
        return false;
    }
    if (waitResult != WAIT_OBJECT_0) {
        cancelOverlappedAndDrain(handle, overlapped);
        return false;
    }

    return GetOverlappedResult(handle, &overlapped, &transferred, FALSE) != FALSE;
}

bool writeAll(HANDLE handle, const std::string& data, ULONGLONG deadline) {
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const DWORD chunk = remaining > static_cast<std::size_t>(MAXDWORD)
            ? MAXDWORD
            : static_cast<DWORD>(remaining);

        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!validHandle(overlapped.hEvent)) {
            return false;
        }

        DWORD written = 0;
        bool ok = true;
        if (!WriteFile(handle, cursor, chunk, &written, &overlapped)) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                ok = false;
            } else {
                ok = waitForOverlapped(handle, overlapped, written, deadline);
            }
        }
        closeHandle(overlapped.hEvent);

        if (!ok || written == 0) {
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

bool readSome(HANDLE handle, char* buffer, DWORD bufferSize, DWORD& read, ULONGLONG deadline) {
    read = 0;

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!validHandle(overlapped.hEvent)) {
        return false;
    }

    bool ok = true;
    if (!ReadFile(handle, buffer, bufferSize, &read, &overlapped)) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            ok = false;
        } else {
            ok = waitForOverlapped(handle, overlapped, read, deadline);
        }
    }
    closeHandle(overlapped.hEvent);
    return ok && read > 0;
}

enum class ConsumeResult {
    NeedMore,
    Complete,
    TooLong
};

ConsumeResult consumePendingLine(std::string& pending, std::string& response) {
    const std::size_t newline = pending.find('\n');
    const std::size_t count = newline == std::string::npos ? pending.size() : newline;

    for (std::size_t i = 0; i < count; ++i) {
        if (pending[i] == '\r') {
            continue;
        }
        if (response.size() == kMaxResponseLineBytes) {
            return ConsumeResult::TooLong;
        }
        response.push_back(pending[i]);
    }

    if (newline != std::string::npos) {
        pending.erase(0, newline + 1);
        return ConsumeResult::Complete;
    }

    pending.clear();
    return ConsumeResult::NeedMore;
}

} // namespace

BackendProcessTransport::~BackendProcessTransport() {
    stop();
}

bool BackendProcessTransport::start(
    const std::string& executablePath,
    const std::string& userDataPath,
    const std::string& filesRoot,
    const std::string& debugSessionToken
) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();

    const std::wstring executablePathW = widenString(executablePath);
    if (executablePathW.empty()) {
        return false;
    }

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrWrite = nullptr;

    if (!createOverlappedPipePair(true, stdinWrite, stdinRead)) {
        return false;
    }
    if (!createOverlappedPipePair(false, stdoutRead, stdoutWrite)) {
        closeHandle(stdinRead);
        closeHandle(stdinWrite);
        return false;
    }
    stderrWrite = createChildStderrHandle();
    if (!validHandle(stderrWrite)) {
        closeHandle(stdinRead);
        closeHandle(stdinWrite);
        closeHandle(stdoutRead);
        closeHandle(stdoutWrite);
        return false;
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);
    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = stdinRead;
    startupInfo.StartupInfo.hStdOutput = stdoutWrite;
    startupInfo.StartupInfo.hStdError = stderrWrite;

    std::vector<HANDLE> inheritedHandles{stdinRead, stdoutWrite, stderrWrite};
    SIZE_T attributeListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
    std::vector<unsigned char> attributeList(attributeListSize);
    startupInfo.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeList.data());
    if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attributeListSize)) {
        closeHandle(stdinRead);
        closeHandle(stdinWrite);
        closeHandle(stdoutRead);
        closeHandle(stdoutWrite);
        closeHandle(stderrWrite);
        return false;
    }

    bool attributesReady = UpdateProcThreadAttribute(
        startupInfo.lpAttributeList,
        0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inheritedHandles.data(),
        inheritedHandles.size() * sizeof(HANDLE),
        nullptr,
        nullptr
    ) != FALSE;

    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine =
        quoteWindowsArg(executablePathW) +
        L" --user-data " + quoteWindowsArg(widenString(userDataPath)) +
        L" --files-root " + quoteWindowsArg(widenString(filesRoot));
    if (!debugSessionToken.empty()) {
        commandLine += L" --debug-session-token " + quoteWindowsArg(widenString(debugSessionToken));
    }
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    BOOL created = FALSE;
    if (attributesReady) {
        created = CreateProcessW(
            executablePathW.c_str(),
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            nullptr,
            &startupInfo.StartupInfo,
            &processInfo
        );
    }

    DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
    closeHandle(stdinRead);
    closeHandle(stdoutWrite);
    closeHandle(stderrWrite);

    if (!created) {
        closeHandle(stdinWrite);
        closeHandle(stdoutRead);
        return false;
    }

    processInfo_ = processInfo;
    childStdinWrite_ = stdinWrite;
    childStdoutRead_ = stdoutRead;
    pendingStdout_.clear();
    return true;
}

bool BackendProcessTransport::requestLine(const std::string& line, std::string& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    response.clear();
    if (!validHandle(childStdinWrite_) || !validHandle(childStdoutRead_) || !validHandle(processInfo_.hProcess)) {
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + kRequestDeadlineMs;
    if (!writeAll(childStdinWrite_, line + "\n", deadline)) {
        stopLocked();
        return false;
    }

    std::array<char, kPipeBufferBytes> buffer{};
    while (true) {
        const ConsumeResult consumeResult = consumePendingLine(pendingStdout_, response);
        if (consumeResult == ConsumeResult::Complete) {
            return true;
        }
        if (consumeResult == ConsumeResult::TooLong) {
            stopLocked();
            return false;
        }

        DWORD read = 0;
        if (!readSome(childStdoutRead_, buffer.data(), static_cast<DWORD>(buffer.size()), read, deadline)) {
            stopLocked();
            return false;
        }
        pendingStdout_.append(buffer.data(), static_cast<std::size_t>(read));
    }
}

void BackendProcessTransport::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
}

void BackendProcessTransport::stopLocked() {
    closeHandle(childStdinWrite_);

    if (validHandle(processInfo_.hProcess)) {
        const DWORD waitResult = WaitForSingleObject(processInfo_.hProcess, kStopGraceMs);
        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(processInfo_.hProcess, 1);
            WaitForSingleObject(processInfo_.hProcess, kPostTerminateWaitMs);
        }
    }

    closeHandle(childStdoutRead_);
    closeHandle(processInfo_.hThread);
    closeHandle(processInfo_.hProcess);
    std::memset(&processInfo_, 0, sizeof(processInfo_));
    pendingStdout_.clear();
}

} // namespace tundraux::frontend

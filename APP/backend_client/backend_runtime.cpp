#include "backend_runtime.hpp"

#include "backend_process.hpp"

#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace tundraux::frontend {
namespace {

std::string pathToString(const std::filesystem::path& path) {
    return path.u8string();
}

std::string randomToken() {
    std::random_device device;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) {
        out << std::setw(8) << device();
    }
    return out.str();
}

bool isDebugStartup(const BackendRuntimeOptions& options) {
    return options.startupUserType == "debug" && !options.startupUserName.empty();
}

std::filesystem::path currentExecutableDirectoryPath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path currentExecutableDirectoryBackendPath() {
    const std::filesystem::path executableDirectory = currentExecutableDirectoryPath();
    return executableDirectory.empty() ? std::filesystem::path{} : executableDirectory / "tundraux_backend_stdio.exe";
}

std::string resolveBackendStdioPath(const std::string& configuredPath, std::string& error) {
    if (!configuredPath.empty()) {
        return configuredPath;
    }

    const std::filesystem::path executableDirectoryPath = currentExecutableDirectoryBackendPath();
    if (executableDirectoryPath.empty()) {
        error = "Failed to resolve frontend executable path for backend stdio process.";
        return "";
    }

    std::error_code existsError;
    if (std::filesystem::exists(executableDirectoryPath, existsError)) {
        return pathToString(executableDirectoryPath);
    }

    error = "Backend stdio executable not found next to frontend executable: " +
        pathToString(executableDirectoryPath);
    return "";
}

std::string resolveFilesRootPath(const std::string& configuredPath, std::string& error) {
    const std::filesystem::path executableDirectory = currentExecutableDirectoryPath();
    if (executableDirectory.empty()) {
        error = "Failed to resolve frontend executable path for files root.";
        return "";
    }

    if (configuredPath.empty()) {
        return pathToString(executableDirectory.lexically_normal());
    }

    const std::filesystem::path filesRoot = configuredPath;
    if (filesRoot.is_absolute()) {
        return pathToString(filesRoot.lexically_normal());
    }

    return pathToString((executableDirectory / filesRoot).lexically_normal());
}

} // namespace

BackendRuntime::BackendRuntime() = default;

BackendRuntime::~BackendRuntime() {
    shutdown();
}

bool BackendRuntime::initialize(const BackendRuntimeOptions& options, std::string& error) {
    shutdown();
    error.clear();
    legacyDirect_ = options.legacyDirect;

    const std::string filesRoot = resolveFilesRootPath(options.filesRoot, error);
    if (filesRoot.empty()) {
        legacyDirect_ = false;
        return false;
    }

    if (legacyDirect_) {
        filesRoot_ = filesRoot;
        return true;
    }

    const std::string backendPath = resolveBackendStdioPath(options.backendStdioPath, error);
    if (backendPath.empty()) {
        legacyDirect_ = false;
        return false;
    }

    const std::string debugSessionToken = isDebugStartup(options) ? randomToken() : std::string{};
    auto transport = std::make_unique<BackendProcessTransport>();
    if (!transport->start(backendPath, options.userDataPath, filesRoot, debugSessionToken)) {
        error = "Failed to start backend stdio process: " + backendPath;
        legacyDirect_ = false;
        return false;
    }

    auto backendClient = std::make_unique<BackendClient>(*transport);
    const auto session = isDebugStartup(options)
        ? backendClient->startDebugSession(debugSessionToken)
        : backendClient->startGuestSession();
    if (!session.ok) {
        error = "Failed to start backend session: " + session.message;
        transport->stop();
        legacyDirect_ = false;
        return false;
    }

    sessionId_ = session.value.sessionId;
    filesRoot_ = filesRoot;
    transport_ = std::move(transport);
    client_ = std::move(backendClient);
    return true;
}

BackendClient* BackendRuntime::client() {
    return client_.get();
}

const BackendClient* BackendRuntime::client() const {
    return client_.get();
}

const std::string& BackendRuntime::sessionId() const {
    return sessionId_;
}

const std::string& BackendRuntime::filesRoot() const {
    return filesRoot_;
}

void BackendRuntime::setSessionId(std::string sessionId) {
    sessionId_ = std::move(sessionId);
}

bool BackendRuntime::legacyDirect() const {
    return legacyDirect_;
}

void BackendRuntime::shutdown() {
    client_.reset();
    if (transport_) {
        transport_->stop();
        transport_.reset();
    }
    sessionId_.clear();
    filesRoot_.clear();
    legacyDirect_ = false;
}

} // namespace tundraux::frontend

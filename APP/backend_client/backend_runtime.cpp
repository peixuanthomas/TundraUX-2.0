#include "backend_runtime.hpp"

#include "backend_process.hpp"

#include <filesystem>
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

std::filesystem::path currentExecutableDirectoryBackendPath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path() /
                "tundraux_backend_stdio.exe";
        }
        buffer.resize(buffer.size() * 2);
    }
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

} // namespace

BackendRuntime::BackendRuntime() = default;

BackendRuntime::~BackendRuntime() {
    shutdown();
}

bool BackendRuntime::initialize(const BackendRuntimeOptions& options, std::string& error) {
    shutdown();
    error.clear();
    legacyDirect_ = options.legacyDirect;

    if (legacyDirect_) {
        return true;
    }

    const std::string backendPath = resolveBackendStdioPath(options.backendStdioPath, error);
    if (backendPath.empty()) {
        legacyDirect_ = false;
        return false;
    }

    auto transport = std::make_unique<BackendProcessTransport>();
    if (!transport->start(backendPath, options.userDataPath, options.filesRoot)) {
        error = "Failed to start backend stdio process: " + backendPath;
        legacyDirect_ = false;
        return false;
    }

    auto backendClient = std::make_unique<BackendClient>(*transport);
    const auto session = backendClient->startGuestSession();
    if (!session.ok) {
        error = "Failed to start backend guest session: " + session.message;
        transport->stop();
        legacyDirect_ = false;
        return false;
    }

    sessionId_ = session.value.sessionId;
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
    legacyDirect_ = false;
}

} // namespace tundraux::frontend

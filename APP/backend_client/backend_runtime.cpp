#include "backend_runtime.hpp"

#include "backend_process.hpp"

#include <filesystem>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace tundraux::frontend {
namespace {

std::string currentExecutableDirectoryBackendPath() {
    char buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return "";
    }

    std::filesystem::path executablePath(buffer);
    return (executablePath.parent_path() / "tundraux_backend_stdio.exe").string();
}

std::string resolveBackendStdioPath(const std::string& configuredPath) {
    if (!configuredPath.empty()) {
        return configuredPath;
    }

    const std::string executableDirectoryPath = currentExecutableDirectoryBackendPath();
    if (!executableDirectoryPath.empty() && std::filesystem::exists(executableDirectoryPath)) {
        return executableDirectoryPath;
    }

    const std::filesystem::path currentDirectoryPath =
        std::filesystem::current_path() / "tundraux_backend_stdio.exe";
    return currentDirectoryPath.string();
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

    const std::string backendPath = resolveBackendStdioPath(options.backendStdioPath);
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

#pragma once

#include "backend_client.hpp"

#include <mutex>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace tundraux::frontend {

class BackendProcessTransport final : public BackendLineTransport {
public:
    BackendProcessTransport() = default;
    ~BackendProcessTransport() override;

    BackendProcessTransport(const BackendProcessTransport&) = delete;
    BackendProcessTransport& operator=(const BackendProcessTransport&) = delete;

    bool start(
        const std::string& executablePath,
        const std::string& userDataPath,
        const std::string& filesRoot
    );
    bool requestLine(const std::string& line, std::string& response) override;
    void stop();

private:
    void stopLocked();

    PROCESS_INFORMATION processInfo_{};
    HANDLE childStdinWrite_ = nullptr;
    HANDLE childStdoutRead_ = nullptr;
    std::string pendingStdout_;
    std::mutex mutex_;
};

} // namespace tundraux::frontend

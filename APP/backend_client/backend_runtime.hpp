#pragma once

#include "backend_client.hpp"

#include <memory>
#include <string>

namespace tundraux::frontend {

class BackendProcessTransport;

struct BackendRuntimeOptions {
    bool legacyDirect = false;
    std::string backendStdioPath;
    std::string userDataPath = "user_data.dat";
    std::string filesRoot = "Files";
};

class BackendRuntime {
public:
    BackendRuntime();
    ~BackendRuntime();

    BackendRuntime(const BackendRuntime&) = delete;
    BackendRuntime& operator=(const BackendRuntime&) = delete;

    bool initialize(const BackendRuntimeOptions& options, std::string& error);
    BackendClient* client();
    const BackendClient* client() const;
    const std::string& sessionId() const;
    void setSessionId(std::string sessionId);
    bool legacyDirect() const;
    void shutdown();

private:
    std::unique_ptr<BackendProcessTransport> transport_;
    std::unique_ptr<BackendClient> client_;
    std::string sessionId_;
    bool legacyDirect_ = false;
};

} // namespace tundraux::frontend

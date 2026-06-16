#pragma once

#include "backend_client.hpp"

#include <memory>
#include <string>

namespace tundraux::frontend {

class BackendProcessTransport;

struct BackendRuntimeOptions {
    std::string backendStdioPath;
    std::string filesRoot;
    std::string startupUserType = "guest";
    std::string startupUserName;
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
    const std::string& filesRoot() const;
    void setSessionId(std::string sessionId);
    void shutdown();

private:
    std::unique_ptr<BackendProcessTransport> transport_;
    std::unique_ptr<BackendClient> client_;
    std::string sessionId_;
    std::string filesRoot_;
};

} // namespace tundraux::frontend

#pragma once

#include "backend_client.hpp"
#include "backend_runtime.hpp"

#include <string>
#include <vector>

namespace tundraux::frontend {

struct ShellUser {
    std::string type = "guest";
    std::string name;
    std::string passwordHint;
    int failedCount = 0;
};

struct FacadeResult {
    bool ok = false;
    std::string message;
    std::string errorCode;
};

class BackendFacade {
public:
    explicit BackendFacade(BackendRuntime& runtime);

    bool active() const;
    bool ensureSession(std::string& message);
    ClientResult<ShellUser> refreshProfile();
    FacadeResult logEvent(const std::string& category, const std::string& detail);
    FacadeResult logKeyPress(const std::string& key, bool sensitive);
    ClientResult<bool> getStrictMode();
    FacadeResult setStrictMode(bool enabled);
    ClientResult<std::vector<std::string>> readTlog(const std::string& path);
    ClientResult<std::string> exportTlog(const std::string& path);

private:
    BackendRuntime& runtime_;
};

} // namespace tundraux::frontend

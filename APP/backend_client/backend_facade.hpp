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

class FrontendAuditSink {
public:
    virtual ~FrontendAuditSink() = default;

    virtual void setCurrentUser(const ShellUser& user) = 0;
    virtual FacadeResult logEvent(const std::string& category, const std::string& detail) = 0;
    virtual FacadeResult logKeyPress(const std::string& key, bool sensitive) = 0;
};

class BackendFacade {
public:
    explicit BackendFacade(BackendRuntime& runtime);

    bool active() const;
    bool ensureSession(std::string& message);
    ClientResult<ShellUser> login(const std::string& username, const std::string& password);
    FacadeResult logout();
    ClientResult<std::vector<ShellUser>> listUsers();
    FacadeResult updateOwnAccount(
        bool passwordProvided,
        const std::string& password,
        bool passwordHintProvided,
        const std::string& passwordHint
    );
    ClientResult<ShellUser> debugForceLogin(const std::string& username);
    ClientResult<ShellUser> refreshProfile();
    FacadeResult logEvent(const std::string& category, const std::string& detail);
    FacadeResult logKeyPress(const std::string& key, bool sensitive);
    ClientResult<bool> getStrictMode();
    FacadeResult setStrictMode(bool enabled);
    FacadeResult createInitialAdmin(
        const std::string& username,
        const std::string& password,
        const std::string& passwordHint
    );
    ClientResult<std::vector<std::string>> readTlog(const std::string& path);
    ClientResult<std::string> exportTlog(const std::string& path);

private:
    BackendRuntime& runtime_;
};

ShellUser shellUserFromFrontendUser(const FrontendUser& user);

class BackendAuditSink : public FrontendAuditSink {
public:
    explicit BackendAuditSink(BackendFacade& facade) : facade_(facade) {}

    void setCurrentUser(const ShellUser& user) override {
        (void)user;
    }

    FacadeResult logEvent(const std::string& category, const std::string& detail) override {
        return facade_.logEvent(category, detail);
    }

    FacadeResult logKeyPress(const std::string& key, bool sensitive) override {
        return facade_.logKeyPress(key, sensitive);
    }

private:
    BackendFacade& facade_;
};

} // namespace tundraux::frontend

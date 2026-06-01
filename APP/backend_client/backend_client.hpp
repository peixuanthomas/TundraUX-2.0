#pragma once

#include <string>
#include <vector>

namespace tundraux::frontend {

struct FrontendUser {
    std::string name;
    std::string type;
};

struct FrontendSession {
    std::string sessionId;
    FrontendUser user;
};

struct FrontendFileEntry {
    std::string name;
    std::string path;
    std::string type;
    unsigned long long size = 0;
};

template <typename T>
struct ClientResult {
    bool ok = false;
    T value{};
    std::string errorCode;
    std::string message;
};

class BackendLineTransport {
public:
    virtual ~BackendLineTransport() = default;
    virtual bool requestLine(const std::string& line, std::string& response) = 0;
};

class BackendClient {
public:
    explicit BackendClient(BackendLineTransport& transport);

    ClientResult<FrontendSession> startGuestSession();
    ClientResult<FrontendSession> startSession(const FrontendUser& user);
    ClientResult<FrontendSession> login(
        const std::string& sessionId,
        const std::string& username,
        const std::string& password
    );
    ClientResult<bool> logout(const std::string& sessionId);
    ClientResult<FrontendUser> whoami(const std::string& sessionId);
    ClientResult<std::vector<FrontendUser>> listUsers(const std::string& sessionId);
    ClientResult<std::vector<FrontendFileEntry>> listDirectory(
        const std::string& sessionId,
        const std::string& path
    );
    ClientResult<std::string> readFile(const std::string& sessionId, const std::string& path);
    ClientResult<bool> writeFile(
        const std::string& sessionId,
        const std::string& path,
        const std::string& content
    );

private:
    BackendLineTransport& transport_;
    unsigned long long nextId_ = 1;

    std::string nextRequestId();
};

} // namespace tundraux::frontend

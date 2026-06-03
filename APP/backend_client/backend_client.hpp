#pragma once

#include <string>
#include <vector>

namespace tundraux::frontend {

struct FrontendUser {
    std::string name;
    std::string type;
    std::string passwordHint;
    int failedCount = 0;
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

struct FrontendTuxContent {
    std::string content;
    std::string creator;
    std::string lastEditor;
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
    ClientResult<FrontendSession> startDebugSession(const std::string& token);
    ClientResult<FrontendSession> login(
        const std::string& sessionId,
        const std::string& username,
        const std::string& password
    );
    ClientResult<bool> logout(const std::string& sessionId);
    ClientResult<FrontendUser> whoami(const std::string& sessionId);
    ClientResult<std::vector<FrontendUser>> listUsers(const std::string& sessionId);
    ClientResult<FrontendUser> currentProfile(const std::string& sessionId);
    ClientResult<bool> createInitialAdmin(
        const std::string& sessionId,
        const std::string& username,
        const std::string& password,
        const std::string& passwordHint
    );
    ClientResult<bool> createUser(
        const std::string& sessionId,
        const FrontendUser& user,
        const std::string& password
    );
    ClientResult<bool> updateUser(
        const std::string& sessionId,
        const std::string& originalName,
        const FrontendUser& user,
        bool passwordProvided,
        const std::string& password
    );
    ClientResult<bool> deleteUser(const std::string& sessionId, const std::string& name);
    ClientResult<bool> resetFailedCount(const std::string& sessionId, const std::string& name);
    ClientResult<bool> disableUser(const std::string& sessionId, const std::string& name);
    ClientResult<bool> updateOwnAccount(
        const std::string& sessionId,
        bool passwordProvided,
        const std::string& password,
        bool passwordHintProvided,
        const std::string& passwordHint
    );
    ClientResult<bool> logAuditEvent(
        const std::string& sessionId,
        const std::string& category,
        const std::string& detail
    );
    ClientResult<bool> logAuditKeyPress(
        const std::string& sessionId,
        const std::string& key,
        bool sensitive
    );
    ClientResult<std::vector<std::string>> readTlog(const std::string& sessionId, const std::string& path);
    ClientResult<std::string> exportTlog(const std::string& sessionId, const std::string& path);
    ClientResult<bool> getStrictMode(const std::string& sessionId);
    ClientResult<bool> setStrictMode(const std::string& sessionId, bool enabled);
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
    ClientResult<bool> deleteFile(const std::string& sessionId, const std::string& path);
    ClientResult<bool> renameFile(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ClientResult<bool> copyFile(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ClientResult<bool> moveFile(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ClientResult<bool> createDirectory(const std::string& sessionId, const std::string& path);
    ClientResult<bool> removeDirectory(const std::string& sessionId, const std::string& path, bool recursive);
    ClientResult<std::vector<FrontendFileEntry>> searchFiles(
        const std::string& sessionId,
        const std::string& root,
        const std::string& query
    );
    ClientResult<std::vector<FrontendFileEntry>> listTux(const std::string& sessionId, const std::string& path);
    ClientResult<bool> createTux(const std::string& sessionId, const std::string& path, bool overwrite);
    ClientResult<FrontendTuxContent> readTux(const std::string& sessionId, const std::string& path);
    ClientResult<bool> writeTux(const std::string& sessionId, const std::string& path, const std::string& content);
    ClientResult<bool> deleteTux(const std::string& sessionId, const std::string& path);
    ClientResult<bool> renameTux(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ClientResult<bool> copyTux(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ClientResult<bool> moveTux(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ClientResult<std::vector<FrontendFileEntry>> searchTux(
        const std::string& sessionId,
        const std::string& root,
        const std::string& query
    );

private:
    BackendLineTransport& transport_;
    unsigned long long nextId_ = 1;

    std::string nextRequestId();
};

} // namespace tundraux::frontend

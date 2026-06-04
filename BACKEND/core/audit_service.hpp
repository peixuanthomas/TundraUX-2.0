#pragma once

#include "file_store.hpp"
#include "session_service.hpp"
#include "user_store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tundraux::backend {

struct AuditRecord {
    std::string category;
    std::string detail;
    bool sensitive = false;
};

struct AuditReadResult {
    std::vector<std::string> lines;
};

class AuditService {
public:
    AuditService(UserStore& users, const SessionService& sessions, std::string logsRoot);

    ServiceResult<EmptyResult> logEvent(
        const std::string& sessionId,
        const std::string& category,
        const std::string& detail
    );

    ServiceResult<EmptyResult> logKeyPress(
        const std::string& sessionId,
        const std::string& key,
        bool sensitive
    );

    ServiceResult<AuditReadResult> readTlog(
        const std::string& sessionId,
        const std::string& path
    ) const;

    ServiceResult<FileContent> exportTlog(
        const std::string& sessionId,
        const std::string& path
    ) const;

private:
    ServiceResult<BackendUser> resolveSessionUser(const std::string& sessionId) const;
    ServiceResult<BackendUser> resolveAppendUser(const std::string& sessionId) const;
    ServiceResult<EmptyResult> appendRecord(const BackendUser& user, const AuditRecord& record);
    ServiceResult<AuditReadResult> readRecords(const std::filesystem::path& absolutePath) const;
    ServiceResult<std::filesystem::path> resolveLogPath(const std::string& path) const;

    static std::string formatRecordLine(const std::string& category, const BackendUser& user, const std::string& detail);
    static std::string sanitizeKey(const std::string& key);
    static std::string obfuscate(const std::string& value);
    static std::string deobfuscate(const std::string& value);
    static bool isPathInsideRoot(std::filesystem::path candidate, const std::filesystem::path& root);
    static bool isSyntheticDebug(const BackendUser& user);
    static bool isPrivileged(const BackendUser& user);

    UserStore& users_;
    const SessionService& sessions_;
    std::string logsRoot_;
    std::filesystem::path startupLogPath_;
};

} // namespace tundraux::backend

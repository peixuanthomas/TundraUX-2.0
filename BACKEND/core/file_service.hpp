#pragma once

#include "file_store.hpp"
#include "session_service.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class FileService {
public:
    FileService(FileStore& files, const SessionService& sessions);

    ServiceResult<std::vector<FileEntry>> listDirectory(
        const std::string& sessionId,
        const std::string& path
    ) const;
    ServiceResult<FileContent> readFile(
        const std::string& sessionId,
        const std::string& path
    ) const;
    ServiceResult<EmptyResult> writeFile(
        const std::string& sessionId,
        const std::string& path,
        const std::string& content
    );
    ServiceResult<EmptyResult> deleteFile(const std::string& sessionId, const std::string& path);
    ServiceResult<EmptyResult> renameFile(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ServiceResult<EmptyResult> copyFile(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ServiceResult<EmptyResult> moveFile(
        const std::string& sessionId,
        const std::string& from,
        const std::string& to,
        bool overwrite
    );
    ServiceResult<EmptyResult> createDirectory(const std::string& sessionId, const std::string& path);
    ServiceResult<EmptyResult> removeDirectory(const std::string& sessionId, const std::string& path, bool recursive);
    ServiceResult<std::vector<FileEntry>> search(
        const std::string& sessionId,
        const std::string& root,
        const std::string& query
    ) const;

private:
    FileStore& files_;
    const SessionService& sessions_;

    ServiceResult<BackendUser> requireFileAccess(const std::string& sessionId) const;
};

} // namespace tundraux::backend

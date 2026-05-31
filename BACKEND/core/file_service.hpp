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

private:
    FileStore& files_;
    const SessionService& sessions_;

    ServiceResult<BackendUser> requireFileAccess(const std::string& sessionId) const;
};

} // namespace tundraux::backend

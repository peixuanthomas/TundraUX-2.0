#pragma once

#include "session_service.hpp"
#include "tux_store.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class TuxService {
public:
    TuxService(TuxStore& store, const SessionService& sessions);

    ServiceResult<std::vector<FileEntry>> list(const std::string& sessionId, const std::string& path) const;
    ServiceResult<TuxContent> read(const std::string& sessionId, const std::string& path) const;
    ServiceResult<EmptyResult> create(const std::string& sessionId, const std::string& path, bool overwrite);
    ServiceResult<EmptyResult> write(const std::string& sessionId, const std::string& path, const std::string& content);
    ServiceResult<EmptyResult> deleteFile(const std::string& sessionId, const std::string& path);
    ServiceResult<EmptyResult> renameFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
    ServiceResult<EmptyResult> copyFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
    ServiceResult<EmptyResult> moveFile(const std::string& sessionId, const std::string& from, const std::string& to, bool overwrite);
    ServiceResult<std::vector<FileEntry>> search(const std::string& sessionId, const std::string& root, const std::string& query) const;

private:
    TuxStore& store_;
    const SessionService& sessions_;

    ServiceResult<BackendUser> requireTuxAccess(const std::string& sessionId) const;
    bool canAccess(const BackendUser& user, const TuxMetadata& metadata) const;
    TuxMetadata newMetadata(const BackendUser& user) const;
    TuxMetadata updatedMetadata(const BackendUser& user, TuxMetadata metadata) const;
};

} // namespace tundraux::backend

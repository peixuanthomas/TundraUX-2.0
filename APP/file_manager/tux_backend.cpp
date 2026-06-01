#include "tux_backend.hpp"

#include <utility>

namespace tundraux::file_manager {
namespace {

template <typename T>
TuxBackendResult<T> fromClientResult(const tundraux::frontend::ClientResult<T>& result) {
    return TuxBackendResult<T>{result.ok, result.value, result.errorCode, result.message};
}

} // namespace

BackendClientTuxBackend::BackendClientTuxBackend(
    tundraux::frontend::BackendClient& client,
    std::string sessionId
) : client_(client), sessionId_(std::move(sessionId)) {}

TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> BackendClientTuxBackend::list(
    const std::string& path
) {
    return fromClientResult(client_.listTux(sessionId_, path));
}

TuxBackendResult<bool> BackendClientTuxBackend::create(const std::string& path, bool overwrite) {
    return fromClientResult(client_.createTux(sessionId_, path, overwrite));
}

TuxBackendResult<tundraux::frontend::FrontendTuxContent> BackendClientTuxBackend::read(const std::string& path) {
    return fromClientResult(client_.readTux(sessionId_, path));
}

TuxBackendResult<bool> BackendClientTuxBackend::write(const std::string& path, const std::string& content) {
    return fromClientResult(client_.writeTux(sessionId_, path, content));
}

TuxBackendResult<bool> BackendClientTuxBackend::deleteFile(const std::string& path) {
    return fromClientResult(client_.deleteTux(sessionId_, path));
}

TuxBackendResult<bool> BackendClientTuxBackend::renameFile(
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return fromClientResult(client_.renameTux(sessionId_, from, to, overwrite));
}

TuxBackendResult<bool> BackendClientTuxBackend::copyFile(
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return fromClientResult(client_.copyTux(sessionId_, from, to, overwrite));
}

TuxBackendResult<bool> BackendClientTuxBackend::moveFile(
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return fromClientResult(client_.moveTux(sessionId_, from, to, overwrite));
}

TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> BackendClientTuxBackend::search(
    const std::string& root,
    const std::string& query
) {
    return fromClientResult(client_.searchTux(sessionId_, root, query));
}

TuxBackendResult<bool> BackendClientTuxBackend::createDirectory(const std::string& path) {
    return fromClientResult(client_.createDirectory(sessionId_, path));
}

TuxBackendResult<bool> BackendClientTuxBackend::removeDirectory(const std::string& path, bool recursive) {
    return fromClientResult(client_.removeDirectory(sessionId_, path, recursive));
}

} // namespace tundraux::file_manager

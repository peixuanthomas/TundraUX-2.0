#include "explorer_backend.hpp"

#include <filesystem>
#include <system_error>
#include <utility>

namespace tundraux::explorer {
namespace {

template <typename T>
ExplorerBackendResult<T> fromClientResult(const tundraux::frontend::ClientResult<T>& result) {
    return ExplorerBackendResult<T>{result.ok, result.value, result.errorCode, result.message};
}

FileEntry toExplorerEntry(const tundraux::frontend::FrontendFileEntry& value) {
    FileEntry entry;
    entry.name = value.name;
    entry.path = fs::u8path(value.path);
    entry.isDirectory = value.type == "directory";
    entry.isHidden = !entry.name.empty() && entry.name.front() == '.';
    entry.size = static_cast<std::uintmax_t>(value.size);
    return entry;
}

ExplorerBackendResult<std::vector<FileEntry>> fromEntryClientResult(
    const tundraux::frontend::ClientResult<std::vector<tundraux::frontend::FrontendFileEntry>>& result
) {
    if (!result.ok) {
        return {false, {}, result.errorCode, result.message};
    }
    std::vector<FileEntry> entries;
    entries.reserve(result.value.size());
    for (const auto& entry : result.value) {
        entries.push_back(toExplorerEntry(entry));
    }
    return {true, std::move(entries), "", ""};
}

} // namespace

BackendClientExplorerBackend::BackendClientExplorerBackend(
    tundraux::frontend::BackendClient& client,
    std::string sessionId
) : client_(client), sessionId_(std::move(sessionId)) {}

ExplorerBackendResult<std::vector<FileEntry>> BackendClientExplorerBackend::listDirectory(const std::string& path) {
    return fromEntryClientResult(client_.listDirectory(sessionId_, path));
}

ExplorerBackendResult<bool> BackendClientExplorerBackend::createDirectory(const std::string& path) {
    return fromClientResult(client_.createDirectory(sessionId_, path));
}

ExplorerBackendResult<bool> BackendClientExplorerBackend::deletePath(const std::string& path, bool recursive) {
    return recursive
        ? fromClientResult(client_.removeDirectory(sessionId_, path, true))
        : fromClientResult(client_.deleteFile(sessionId_, path));
}

ExplorerBackendResult<bool> BackendClientExplorerBackend::copyPath(
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return fromClientResult(client_.copyFile(sessionId_, from, to, overwrite));
}

ExplorerBackendResult<bool> BackendClientExplorerBackend::movePath(
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return fromClientResult(client_.moveFile(sessionId_, from, to, overwrite));
}

ExplorerBackendResult<std::vector<FileEntry>> BackendClientExplorerBackend::search(
    const std::string& root,
    const std::string& query
) {
    return fromEntryClientResult(client_.searchFiles(sessionId_, root, query));
}

std::string explorerRelativePath(const fs::path& root, const fs::path& path) {
    std::error_code error;
    const fs::path relative = fs::relative(path, root, error);
    if (error) {
        return path.u8string();
    }
    const std::string value = relative.generic_u8string();
    return value == "." ? std::string{} : value;
}

} // namespace tundraux::explorer

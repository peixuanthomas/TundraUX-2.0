#pragma once

#include "backend_client.hpp"
#include "explorer_types.hpp"

#include <string>
#include <vector>

namespace tundraux::explorer {

template <typename T>
struct ExplorerBackendResult {
    bool ok = false;
    T value{};
    std::string errorCode;
    std::string message;
};

class ExplorerBackend {
public:
    virtual ~ExplorerBackend() = default;
    virtual ExplorerBackendResult<std::vector<FileEntry>> listDirectory(const std::string& path) = 0;
    virtual ExplorerBackendResult<bool> createDirectory(const std::string& path) = 0;
    virtual ExplorerBackendResult<bool> deletePath(const std::string& path, bool recursive) = 0;
    virtual ExplorerBackendResult<bool> copyPath(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual ExplorerBackendResult<bool> movePath(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual ExplorerBackendResult<std::vector<FileEntry>> search(const std::string& root, const std::string& query) = 0;
    virtual ExplorerBackendResult<std::string> readFile(const std::string& path) = 0;
    virtual ExplorerBackendResult<bool> writeFile(const std::string& path, const std::string& content) = 0;
    virtual ExplorerBackendResult<::tundraux::frontend::FrontendTuxContent> readTux(const std::string& path) = 0;
    virtual ExplorerBackendResult<bool> writeTux(const std::string& path, const std::string& content) = 0;
    virtual ExplorerBackendResult<std::vector<std::string>> readTlog(const std::string& path) = 0;
};

class BackendClientExplorerBackend final : public ExplorerBackend {
public:
    BackendClientExplorerBackend(::tundraux::frontend::BackendClient& client, std::string sessionId);

    ExplorerBackendResult<std::vector<FileEntry>> listDirectory(const std::string& path) override;
    ExplorerBackendResult<bool> createDirectory(const std::string& path) override;
    ExplorerBackendResult<bool> deletePath(const std::string& path, bool recursive) override;
    ExplorerBackendResult<bool> copyPath(const std::string& from, const std::string& to, bool overwrite) override;
    ExplorerBackendResult<bool> movePath(const std::string& from, const std::string& to, bool overwrite) override;
    ExplorerBackendResult<std::vector<FileEntry>> search(const std::string& root, const std::string& query) override;
    ExplorerBackendResult<std::string> readFile(const std::string& path) override;
    ExplorerBackendResult<bool> writeFile(const std::string& path, const std::string& content) override;
    ExplorerBackendResult<::tundraux::frontend::FrontendTuxContent> readTux(const std::string& path) override;
    ExplorerBackendResult<bool> writeTux(const std::string& path, const std::string& content) override;
    ExplorerBackendResult<std::vector<std::string>> readTlog(const std::string& path) override;

private:
    ::tundraux::frontend::BackendClient& client_;
    std::string sessionId_;
};

std::string explorerRelativePath(const fs::path& root, const fs::path& path);

} // namespace tundraux::explorer

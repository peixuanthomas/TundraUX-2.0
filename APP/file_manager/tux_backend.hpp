#pragma once

#include "backend_client.hpp"

#include <string>
#include <vector>

namespace tundraux::file_manager {

template <typename T>
struct TuxBackendResult {
    bool ok = false;
    T value{};
    std::string errorCode;
    std::string message;
};

class TuxBackend {
public:
    virtual ~TuxBackend() = default;
    virtual TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> list(const std::string& path) = 0;
    virtual TuxBackendResult<bool> create(const std::string& path, bool overwrite) = 0;
    virtual TuxBackendResult<tundraux::frontend::FrontendTuxContent> read(const std::string& path) = 0;
    virtual TuxBackendResult<bool> write(const std::string& path, const std::string& content) = 0;
    virtual TuxBackendResult<bool> deleteFile(const std::string& path) = 0;
    virtual TuxBackendResult<bool> renameFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual TuxBackendResult<bool> copyFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual TuxBackendResult<bool> moveFile(const std::string& from, const std::string& to, bool overwrite) = 0;
    virtual TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> search(
        const std::string& root,
        const std::string& query
    ) = 0;
    virtual TuxBackendResult<bool> createDirectory(const std::string& path) = 0;
    virtual TuxBackendResult<bool> removeDirectory(const std::string& path, bool recursive) = 0;
};

class BackendClientTuxBackend final : public TuxBackend {
public:
    BackendClientTuxBackend(tundraux::frontend::BackendClient& client, std::string sessionId);

    TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> list(const std::string& path) override;
    TuxBackendResult<bool> create(const std::string& path, bool overwrite) override;
    TuxBackendResult<tundraux::frontend::FrontendTuxContent> read(const std::string& path) override;
    TuxBackendResult<bool> write(const std::string& path, const std::string& content) override;
    TuxBackendResult<bool> deleteFile(const std::string& path) override;
    TuxBackendResult<bool> renameFile(const std::string& from, const std::string& to, bool overwrite) override;
    TuxBackendResult<bool> copyFile(const std::string& from, const std::string& to, bool overwrite) override;
    TuxBackendResult<bool> moveFile(const std::string& from, const std::string& to, bool overwrite) override;
    TuxBackendResult<std::vector<tundraux::frontend::FrontendFileEntry>> search(
        const std::string& root,
        const std::string& query
    ) override;
    TuxBackendResult<bool> createDirectory(const std::string& path) override;
    TuxBackendResult<bool> removeDirectory(const std::string& path, bool recursive) override;

private:
    tundraux::frontend::BackendClient& client_;
    std::string sessionId_;
};

} // namespace tundraux::file_manager

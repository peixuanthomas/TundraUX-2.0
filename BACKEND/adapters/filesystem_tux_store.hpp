#pragma once

#include "tux_store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tundraux::backend {

class FilesystemTuxStore final : public TuxStore {
public:
    explicit FilesystemTuxStore(std::string root);

    std::vector<FileEntry> list(const std::string& path) const override;
    TuxMetadata metadata(const std::string& path) const override;
    TuxContent read(const std::string& path) const override;
    void create(const std::string& path, const TuxMetadata& metadata, bool overwrite) override;
    void write(const std::string& path, const std::string& content, const TuxMetadata& metadata) override;
    void deleteFile(const std::string& path) override;
    void renameFile(const std::string& from, const std::string& to, bool overwrite) override;
    void copyFile(const std::string& from, const std::string& to, const TuxMetadata& metadata, bool overwrite) override;
    void moveFile(const std::string& from, const std::string& to, bool overwrite) override;
    std::vector<FileEntry> search(const std::string& root, const std::string& query) const override;

private:
    std::filesystem::path configuredRoot_;
    std::filesystem::path root_;

    void ensureTrustedRoot() const;
    void rejectUnsafeExistingPathComponents(const std::filesystem::path& resolved) const;
    std::filesystem::path resolveDirectoryPath(const std::string& path, bool allowRoot) const;
    std::filesystem::path resolveFilePath(const std::string& path) const;
    FileEntry entryFromPath(const std::filesystem::path& path) const;
};

} // namespace tundraux::backend

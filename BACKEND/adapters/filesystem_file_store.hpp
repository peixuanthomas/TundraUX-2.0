#pragma once

#include "file_store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tundraux::backend {

class FilesystemFileStore final : public FileStore {
public:
    explicit FilesystemFileStore(std::string root);

    std::vector<FileEntry> listDirectory(const std::string& path) const override;
    FileContent readFile(const std::string& path) const override;
    void writeFile(const std::string& path, const std::string& content) override;
    void deleteFile(const std::string& path) override;
    void renameFile(const std::string& from, const std::string& to, bool overwrite) override;
    void copyFile(const std::string& from, const std::string& to, bool overwrite) override;
    void moveFile(const std::string& from, const std::string& to, bool overwrite) override;
    void createDirectory(const std::string& path) override;
    void removeDirectory(const std::string& path, bool recursive) override;
    std::vector<FileEntry> search(const std::string& root, const std::string& query) const override;

private:
    std::filesystem::path configuredRoot_;
    std::filesystem::path root_;

    void ensureTrustedRoot() const;
    std::filesystem::path resolveManagedPath(const std::string& path, bool allowRoot) const;
    void rejectUnsafeExistingPathComponents(const std::filesystem::path& resolved) const;
    void rejectProtectedPath(const std::filesystem::path& resolved) const;
    void rejectSamePath(const std::filesystem::path& from, const std::filesystem::path& to) const;
    void rejectExistingDestination(const std::filesystem::path& destination, bool overwrite) const;
    FileEntry entryFromPath(const std::filesystem::path& path) const;
};

} // namespace tundraux::backend

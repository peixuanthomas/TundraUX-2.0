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

private:
    std::filesystem::path root_;

    std::filesystem::path resolveManagedPath(const std::string& path, bool allowRoot) const;
    void rejectProtectedPath(const std::filesystem::path& resolved) const;
};

} // namespace tundraux::backend

#pragma once

#include "explorer_types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tundraux::explorer {
namespace fs = std::filesystem;

struct DirectoryStats {
    std::uintmax_t directChildren = 0;
    std::uintmax_t recursiveFiles = 0;
    std::uintmax_t recursiveDirectories = 0;
    std::uintmax_t totalFileSize = 0;
    std::uintmax_t scanErrors = 0;
};

bool directoryContainsExtension(const fs::path& directory, const std::string& extension);
bool isHiddenPath(const fs::path& path);
DirectoryStats scanDirectoryStats(const fs::path& path);
std::vector<FileEntry> readDirectory(const fs::path& path, const fs::path& rootPath, bool showHidden);
void refresh(ExplorerState& state);
std::vector<std::string> previewDirectory(const fs::path& path, const fs::path& rootPath);
std::vector<std::string> previewFile(const fs::path& path);
std::vector<std::string> previewSelected(const ExplorerState& state);

}

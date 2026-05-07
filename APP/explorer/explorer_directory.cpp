#include "explorer_directory.hpp"

#include "explorer_text.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <windows.h>

namespace tundraux::explorer {

bool directoryContainsExtension(const fs::path& directory, const std::string& extension) {
    std::error_code error;
    for (const auto& item : fs::recursive_directory_iterator(
             directory,
             fs::directory_options::skip_permission_denied,
             error
         )) {
        if (error) {
            error.clear();
            continue;
        }
        if (!item.is_directory(error) && extensionOf(item.path()) == extension) {
            return true;
        }
        error.clear();
    }
    return false;
}

bool isHiddenPath(const fs::path& path) {
    const std::string filename = path.filename().u8string();
    if (!filename.empty() && filename.front() == '.') {
        return true;
    }

    const DWORD attributes = GetFileAttributesW(path.wstring().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
}

DirectoryStats scanDirectoryStats(const fs::path& path) {
    DirectoryStats stats;
    std::error_code error;

    for (const auto& item : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, error)) {
        if (error) {
            ++stats.scanErrors;
            error.clear();
            continue;
        }
        ++stats.directChildren;
    }

    error.clear();
    fs::recursive_directory_iterator iterator(path, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (iterator != end) {
        if (error) {
            ++stats.scanErrors;
            error.clear();
            iterator.increment(error);
            continue;
        }

        std::error_code statusError;
        const fs::file_status status = iterator->symlink_status(statusError);
        if (statusError) {
            ++stats.scanErrors;
        } else if (fs::is_directory(status)) {
            ++stats.recursiveDirectories;
        } else {
            ++stats.recursiveFiles;
            if (fs::is_regular_file(status)) {
                std::error_code sizeError;
                stats.totalFileSize += fs::file_size(iterator->path(), sizeError);
                if (sizeError) {
                    ++stats.scanErrors;
                }
            }
        }

        iterator.increment(error);
    }

    if (error) {
        ++stats.scanErrors;
    }

    return stats;
}

std::vector<FileEntry> readDirectory(const fs::path& path, const fs::path& rootPath, bool showHidden) {
    std::vector<FileEntry> entries;
    std::error_code error;

    for (const auto& item : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, error)) {
        std::error_code statusError;
        const fs::file_status status = item.symlink_status(statusError);
        if (statusError) {
            continue;
        }

        const fs::path itemPath = item.path();
        if (!isPathInsideRoot(itemPath, rootPath)) {
            continue;
        }

        const bool hidden = isHiddenPath(itemPath);
        if (!showHidden && hidden) {
            continue;
        }

        FileEntry entry;
        entry.name = itemPath.filename().u8string();
        entry.path = itemPath;
        entry.isDirectory = fs::is_directory(status);
        entry.isHidden = hidden;
        if (!entry.isDirectory) {
            entry.size = fs::file_size(itemPath, statusError);
            if (statusError) {
                entry.size = 0;
            }
        }
        entries.push_back(entry);
    }

    std::stable_sort(entries.begin(), entries.end(), [](const FileEntry& left, const FileEntry& right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory;
        }
        return toLowerCopy(left.name) < toLowerCopy(right.name);
    });

    return entries;
}

void refresh(ExplorerState& state) {
    try {
        if (!isPathInsideRoot(state.currentPath, state.rootPath)) {
            state.currentPath = state.rootPath;
        }

        state.entries = readDirectory(state.currentPath, state.rootPath, state.showHidden);
        state.parentEntries.clear();
        if (!isSamePath(state.currentPath, state.rootPath) && state.currentPath.has_parent_path()) {
            const fs::path parentPath = state.currentPath.parent_path();
            if (isPathInsideRoot(parentPath, state.rootPath)) {
                state.parentEntries = readDirectory(parentPath, state.rootPath, state.showHidden);
            }
        }
        if (state.entries.empty()) {
            state.cursor = 0;
            state.scroll = 0;
        } else {
            state.cursor = std::min(state.cursor, state.entries.size() - 1);
            state.scroll = std::min(state.scroll, state.cursor);
        }
        state.message = std::to_string(state.entries.size()) + " item(s)";
    } catch (const std::exception& ex) {
        state.entries.clear();
        state.message = std::string("Read failed: ") + ex.what();
    }
}

std::vector<std::string> previewDirectory(const fs::path& path, const fs::path& rootPath) {
    std::vector<std::string> lines;
    std::error_code error;
    const auto entries = readDirectory(path, rootPath, false);
    if (entries.empty()) {
        lines.push_back("Empty directory");
        return lines;
    }

    for (const auto& entry : entries) {
        lines.push_back(std::string(entry.isDirectory ? "[D] " : "    ") + entry.name);
        if (lines.size() >= 80) {
            break;
        }
    }
    if (error) {
        lines.push_back("Cannot preview directory");
    }
    return lines;
}

std::vector<std::string> previewFile(const fs::path& path) {
    if (extensionOf(path) == ".tux") {
        return {"Encrypted TUX file.", "Press Enter to decrypt and edit."};
    }
    if (extensionOf(path) == ".dat") {
        return {"User data file."};
    }

    std::vector<std::string> lines;
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (!error && size > 96 * 1024) {
        lines.push_back("File is too large to preview.");
        lines.push_back("Size: " + formatSize(size));
        return lines;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        lines.push_back("Cannot read file.");
        return lines;
    }

    std::string content(4096, '\0');
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(file.gcount()));
    if (content.find('\0') != std::string::npos) {
        lines.push_back("Binary file preview is not supported.");
        return lines;
    }

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line) && lines.size() < 80) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (lines.empty()) {
        lines.push_back("Empty file");
    }
    return lines;
}

std::vector<std::string> previewSelected(const ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        return {"No file selected"};
    }

    const FileEntry& entry = state.entries[state.cursor];
    try {
        return entry.isDirectory ? previewDirectory(entry.path, state.rootPath) : previewFile(entry.path);
    } catch (const std::exception& ex) {
        return {std::string("Preview failed: ") + ex.what()};
    }
}

}

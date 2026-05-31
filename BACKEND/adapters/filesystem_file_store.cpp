#include "filesystem_file_store.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace tundraux::backend {

namespace {

constexpr std::uintmax_t kMaxFileContentSize = 16u * 1024u * 1024u;
constexpr const char* kAccessDeniedMessage = "Access denied.";
constexpr const char* kFileNotFoundMessage = "File not found.";
constexpr const char* kInvalidPathMessage = "Invalid path.";
constexpr const char* kFileStorageErrorMessage = "File storage error.";

BackendException invalidPath() {
    return BackendException(ErrorCode::InvalidPath, kInvalidPathMessage);
}

BackendException notFound() {
    return BackendException(ErrorCode::NotFound, kFileNotFoundMessage);
}

BackendException permissionDenied() {
    return BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
}

BackendException storageError() {
    return BackendException(ErrorCode::StorageError, kFileStorageErrorMessage);
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isPathInside(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto rootIt = root.begin();
    auto pathIt = path.begin();
    for (; rootIt != root.end(); ++rootIt, ++pathIt) {
        if (pathIt == path.end() || *rootIt != *pathIt) {
            return false;
        }
    }
    return true;
}

std::filesystem::path stableAbsolutePath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }
    return std::filesystem::absolute(path).lexically_normal();
}

std::filesystem::path canonicalExistingPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        throw storageError();
    }
    return canonical;
}

} // namespace

FilesystemFileStore::FilesystemFileStore(std::string root)
    : root_(stableAbsolutePath(std::filesystem::path(std::move(root)))) {}

std::vector<FileEntry> FilesystemFileStore::listDirectory(const std::string& path) const {
    try {
        const auto resolved = canonicalExistingPath(resolveManagedPath(path, true));
        if (!isPathInside(root_, resolved)) {
            throw invalidPath();
        }

        std::error_code error;
        if (!std::filesystem::exists(resolved, error)) {
            if (error) {
                throw storageError();
            }
            throw notFound();
        }
        if (!std::filesystem::is_directory(resolved, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }

        std::vector<FileEntry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(resolved)) {
            const auto name = entry.path().filename().string();
            if (name == "temp") {
                continue;
            }

            const auto status = entry.symlink_status(error);
            if (error) {
                throw storageError();
            }

            FileEntry out;
            out.name = name;
            out.path = std::filesystem::relative(entry.path(), root_, error).generic_string();
            if (error) {
                throw storageError();
            }
            out.type = std::filesystem::is_directory(status) ? FileEntryType::Directory : FileEntryType::File;
            if (std::filesystem::is_regular_file(status)) {
                out.size = entry.file_size(error);
                if (error) {
                    throw storageError();
                }
            }
            entries.push_back(std::move(out));
        }

        std::sort(entries.begin(), entries.end(), [](const FileEntry& left, const FileEntry& right) {
            if (left.type != right.type) {
                return left.type == FileEntryType::Directory;
            }
            const auto leftName = lowerAscii(left.name);
            const auto rightName = lowerAscii(right.name);
            if (leftName != rightName) {
                return leftName < rightName;
            }
            return left.name < right.name;
        });
        return entries;
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

FileContent FilesystemFileStore::readFile(const std::string& path) const {
    try {
        const auto resolved = canonicalExistingPath(resolveManagedPath(path, false));
        if (!isPathInside(root_, resolved)) {
            throw invalidPath();
        }
        rejectProtectedPath(resolved);

        std::error_code error;
        if (!std::filesystem::exists(resolved, error)) {
            if (error) {
                throw storageError();
            }
            throw notFound();
        }
        if (!std::filesystem::is_regular_file(resolved, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }

        const auto size = std::filesystem::file_size(resolved, error);
        if (error) {
            throw storageError();
        }
        if (size > kMaxFileContentSize) {
            throw storageError();
        }

        std::ifstream stream(resolved, std::ios::binary);
        if (!stream) {
            throw storageError();
        }
        std::string content;
        content.resize(static_cast<std::size_t>(size));
        if (!content.empty()) {
            stream.read(content.data(), static_cast<std::streamsize>(content.size()));
        }
        if (!stream && !stream.eof()) {
            throw storageError();
        }
        return FileContent{std::move(content)};
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemFileStore::writeFile(const std::string& path, const std::string& content) {
    try {
        if (content.size() > kMaxFileContentSize) {
            throw storageError();
        }

        const auto resolved = resolveManagedPath(path, false);
        const auto parent = canonicalExistingPath(resolved.parent_path());
        const auto finalPath = (parent / resolved.filename()).lexically_normal();
        if (!isPathInside(root_, parent) || !isPathInside(root_, finalPath)) {
            throw invalidPath();
        }
        rejectProtectedPath(finalPath);

        std::error_code error;
        if (std::filesystem::exists(finalPath, error)) {
            const auto existingTarget = canonicalExistingPath(finalPath);
            if (!isPathInside(root_, existingTarget)) {
                throw invalidPath();
            }
            rejectProtectedPath(existingTarget);
        } else if (error) {
            throw storageError();
        }

        std::filesystem::create_directories(parent, error);
        if (error) {
            throw storageError();
        }

        std::ofstream stream(finalPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw storageError();
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!stream) {
            throw storageError();
        }
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

std::filesystem::path FilesystemFileStore::resolveManagedPath(const std::string& path, bool allowRoot) const {
    if (path.empty()) {
        if (allowRoot) {
            return root_;
        }
        throw invalidPath();
    }

    const std::filesystem::path requested(path);
    if (requested.is_absolute()) {
        throw invalidPath();
    }

    for (const auto& part : requested) {
        if (part == "." || part == "..") {
            throw invalidPath();
        }
    }

    const auto resolved = (root_ / requested).lexically_normal();
    if (!isPathInside(root_, resolved)) {
        throw invalidPath();
    }
    return resolved;
}

void FilesystemFileStore::rejectProtectedPath(const std::filesystem::path& resolved) const {
    const auto filename = resolved.filename().string();
    const auto extension = resolved.extension().string();
    if (filename == "user_data.dat" ||
        extension == ".TUX" ||
        extension == ".tux" ||
        extension == ".tlog") {
        throw permissionDenied();
    }
}

} // namespace tundraux::backend

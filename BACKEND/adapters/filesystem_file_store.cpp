#include "filesystem_file_store.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

bool isReservedDosDeviceName(const std::string& component) {
    const auto lower = lowerAscii(component);
    const auto base = lower.substr(0, lower.find('.'));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") {
        return true;
    }
    if (base.size() == 4 &&
        (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) &&
        base[3] >= '1' && base[3] <= '9') {
        return true;
    }
    return false;
}

bool isReparseOrSymlink(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error && std::filesystem::is_symlink(status)) {
        return true;
    }

#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.wstring().c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return true;
    }
#endif

    return false;
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

std::filesystem::path lexicalAbsolutePath(const std::filesystem::path& path) {
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

std::string lexicalEntryPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) {
        return std::filesystem::path(name).generic_string();
    }
    return (std::filesystem::path(directory) / name).generic_string();
}

} // namespace

FilesystemFileStore::FilesystemFileStore(std::string root)
    : configuredRoot_(lexicalAbsolutePath(std::filesystem::path(std::move(root)))),
      root_(stableAbsolutePath(configuredRoot_)) {
    ensureTrustedRoot();
}

std::vector<FileEntry> FilesystemFileStore::listDirectory(const std::string& path) const {
    try {
        ensureTrustedRoot();
        const auto requested = resolveManagedPath(path, true);
        rejectUnsafeExistingPathComponents(requested);
        const auto resolved = canonicalExistingPath(requested);
        if (!isPathInside(root_, resolved)) {
            throw invalidPath();
        }
        if (!path.empty()) {
            rejectProtectedPath(resolved);
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
            if (isReparseOrSymlink(entry.path())) {
                continue;
            }

            FileEntry out;
            out.name = name;
            out.path = lexicalEntryPath(path, name);
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
        ensureTrustedRoot();
        const auto requested = resolveManagedPath(path, false);
        rejectUnsafeExistingPathComponents(requested);
        const auto resolved = canonicalExistingPath(requested);
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
        if (stream.gcount() != static_cast<std::streamsize>(content.size())) {
            throw storageError();
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
        ensureTrustedRoot();
        if (content.size() > kMaxFileContentSize) {
            throw storageError();
        }

        const auto resolved = resolveManagedPath(path, false);
        rejectUnsafeExistingPathComponents(resolved);
        rejectProtectedPath(resolved);
        std::error_code error;
        std::filesystem::create_directories(resolved.parent_path(), error);
        if (error) {
            throw storageError();
        }
        ensureTrustedRoot();
        rejectUnsafeExistingPathComponents(resolved);

        const auto parent = canonicalExistingPath(resolved.parent_path());
        const auto finalPath = (parent / resolved.filename()).lexically_normal();
        if (!isPathInside(root_, parent) || !isPathInside(root_, finalPath)) {
            throw invalidPath();
        }

        if (std::filesystem::exists(finalPath, error)) {
            const auto existingTarget = canonicalExistingPath(finalPath);
            if (!isPathInside(root_, existingTarget)) {
                throw invalidPath();
            }
            rejectProtectedPath(existingTarget);
        } else if (error) {
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

void FilesystemFileStore::ensureTrustedRoot() const {
    std::error_code error;
    const bool exists = std::filesystem::exists(configuredRoot_, error);
    if (error) {
        throw storageError();
    }
    if (!exists) {
        return;
    }
    if (isReparseOrSymlink(configuredRoot_)) {
        throw permissionDenied();
    }
    const auto resolvedRoot = canonicalExistingPath(configuredRoot_);
    if (resolvedRoot != root_) {
        throw permissionDenied();
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
        const auto component = part.string();
        // Windows 8.3 short-name aliases can bypass protected filename checks.
        if (component.empty() ||
            component == "." ||
            component == ".." ||
            component.find(':') != std::string::npos ||
            component.find('~') != std::string::npos ||
            component.back() == '.' ||
            component.back() == ' ' ||
            isReservedDosDeviceName(component)) {
            throw invalidPath();
        }
    }

    const auto resolved = (root_ / requested).lexically_normal();
    if (!isPathInside(root_, resolved)) {
        throw invalidPath();
    }
    return resolved;
}

void FilesystemFileStore::rejectUnsafeExistingPathComponents(const std::filesystem::path& resolved) const {
    const auto relative = resolved.lexically_relative(root_);
    if (relative.empty()) {
        return;
    }

    auto current = root_;
    for (const auto& part : relative) {
        current /= part;
        if (isReparseOrSymlink(current)) {
            throw permissionDenied();
        }
    }
}

void FilesystemFileStore::rejectProtectedPath(const std::filesystem::path& resolved) const {
    std::error_code error;
    const auto relative = std::filesystem::relative(resolved, root_, error);
    if (error) {
        throw storageError();
    }

    for (const auto& part : relative) {
        const auto filename = lowerAscii(part.filename().string());
        const auto extension = lowerAscii(part.extension().string());
        if (filename == "user_data.dat" ||
            extension == ".tux" ||
            extension == ".tlog") {
            throw permissionDenied();
        }
    }
}

} // namespace tundraux::backend

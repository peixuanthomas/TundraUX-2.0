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

bool isProtectedRelativePath(const std::filesystem::path& relative) {
    for (const auto& part : relative) {
        const auto filename = lowerAscii(part.filename().string());
        const auto extension = lowerAscii(part.extension().string());
        if (filename == "user_data.dat" ||
            filename == "temp" ||
            extension == ".tux" ||
            extension == ".tlog") {
            return true;
        }
    }
    return false;
}

bool isCrossDeviceRenameError(const std::error_code& error) {
    if (error == std::make_error_code(std::errc::cross_device_link)) {
        return true;
    }

#ifdef _WIN32
    if (error.category() == std::system_category() && error.value() == ERROR_NOT_SAME_DEVICE) {
        return true;
    }
#endif

    return false;
}

void removeSourceAfterNewDestinationFallbackCopy(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
) {
    std::error_code error;
    if (!std::filesystem::remove(source, error) || error) {
        std::error_code cleanupError;
        std::filesystem::remove(destination, cleanupError);
        throw storageError();
    }
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

void FilesystemFileStore::deleteFile(const std::string& path) {
    try {
        ensureTrustedRoot();
        const auto requested = resolveManagedPath(path, false);
        rejectUnsafeExistingPathComponents(requested);
        rejectProtectedPath(requested);
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
        if (!std::filesystem::remove(resolved, error) || error) {
            throw storageError();
        }
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemFileStore::renameFile(const std::string& from, const std::string& to, bool overwrite) {
    moveFile(from, to, overwrite);
}

void FilesystemFileStore::copyFile(const std::string& from, const std::string& to, bool overwrite) {
    try {
        ensureTrustedRoot();
        const auto requestedSource = resolveManagedPath(from, false);
        const auto requestedDestination = resolveManagedPath(to, false);
        rejectSamePath(requestedSource, requestedDestination);
        rejectUnsafeExistingPathComponents(requestedSource);
        rejectUnsafeExistingPathComponents(requestedDestination);
        rejectProtectedPath(requestedSource);
        rejectProtectedPath(requestedDestination);

        const auto source = canonicalExistingPath(requestedSource);
        if (!isPathInside(root_, source)) {
            throw invalidPath();
        }
        rejectProtectedPath(source);

        std::error_code error;
        if (!std::filesystem::exists(source, error)) {
            if (error) {
                throw storageError();
            }
            throw notFound();
        }
        if (!std::filesystem::is_regular_file(source, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }

        rejectExistingDestination(requestedDestination, overwrite);
        std::filesystem::create_directories(requestedDestination.parent_path(), error);
        if (error) {
            throw storageError();
        }
        ensureTrustedRoot();
        rejectUnsafeExistingPathComponents(requestedDestination);
        const auto parent = canonicalExistingPath(requestedDestination.parent_path());
        const auto destination = (parent / requestedDestination.filename()).lexically_normal();
        if (!isPathInside(root_, parent) || !isPathInside(root_, destination)) {
            throw invalidPath();
        }
        rejectProtectedPath(destination);

        if (std::filesystem::exists(destination, error)) {
            if (error) {
                throw storageError();
            }
            const auto existingTarget = canonicalExistingPath(destination);
            if (!isPathInside(root_, existingTarget)) {
                throw invalidPath();
            }
            rejectProtectedPath(existingTarget);
            if (!std::filesystem::is_regular_file(existingTarget, error)) {
                if (error) {
                    throw storageError();
                }
                throw invalidPath();
            }
        } else if (error) {
            throw storageError();
        }

        const auto options = overwrite
            ? std::filesystem::copy_options::overwrite_existing
            : std::filesystem::copy_options::none;
        if (!std::filesystem::copy_file(source, destination, options, error) || error) {
            throw storageError();
        }
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemFileStore::moveFile(const std::string& from, const std::string& to, bool overwrite) {
    try {
        ensureTrustedRoot();
        const auto requestedSource = resolveManagedPath(from, false);
        const auto requestedDestination = resolveManagedPath(to, false);
        rejectSamePath(requestedSource, requestedDestination);
        rejectUnsafeExistingPathComponents(requestedSource);
        rejectUnsafeExistingPathComponents(requestedDestination);
        rejectProtectedPath(requestedSource);
        rejectProtectedPath(requestedDestination);

        const auto source = canonicalExistingPath(requestedSource);
        if (!isPathInside(root_, source)) {
            throw invalidPath();
        }
        rejectProtectedPath(source);

        std::error_code error;
        if (!std::filesystem::exists(source, error)) {
            if (error) {
                throw storageError();
            }
            throw notFound();
        }
        if (!std::filesystem::is_regular_file(source, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }

        rejectExistingDestination(requestedDestination, overwrite);
        std::filesystem::create_directories(requestedDestination.parent_path(), error);
        if (error) {
            throw storageError();
        }
        ensureTrustedRoot();
        rejectUnsafeExistingPathComponents(requestedDestination);
        const auto parent = canonicalExistingPath(requestedDestination.parent_path());
        const auto destination = (parent / requestedDestination.filename()).lexically_normal();
        if (!isPathInside(root_, parent) || !isPathInside(root_, destination)) {
            throw invalidPath();
        }
        rejectProtectedPath(destination);

        bool destinationExists = false;
        if (std::filesystem::exists(destination, error)) {
            if (error) {
                throw storageError();
            }
            destinationExists = true;
            const auto existingTarget = canonicalExistingPath(destination);
            if (!isPathInside(root_, existingTarget)) {
                throw invalidPath();
            }
            rejectProtectedPath(existingTarget);
            if (!std::filesystem::is_regular_file(existingTarget, error)) {
                if (error) {
                    throw storageError();
                }
                throw invalidPath();
            }
        } else if (error) {
            throw storageError();
        }

        std::filesystem::rename(source, destination, error);
        if (!error) {
            return;
        }
        if (!isCrossDeviceRenameError(error)) {
            throw storageError();
        }
        if (destinationExists) {
            throw storageError();
        }

        error.clear();
        if (!std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error) || error) {
            throw storageError();
        }
        removeSourceAfterNewDestinationFallbackCopy(source, destination);
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemFileStore::createDirectory(const std::string& path) {
    try {
        ensureTrustedRoot();
        const auto resolved = resolveManagedPath(path, false);
        rejectUnsafeExistingPathComponents(resolved);
        rejectProtectedPath(resolved);
        std::error_code error;
        std::filesystem::create_directories(resolved, error);
        if (error) {
            throw storageError();
        }
        ensureTrustedRoot();
        rejectUnsafeExistingPathComponents(resolved);
        const auto created = canonicalExistingPath(resolved);
        if (!isPathInside(root_, created)) {
            throw invalidPath();
        }
        rejectProtectedPath(created);
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemFileStore::removeDirectory(const std::string& path, bool recursive) {
    try {
        ensureTrustedRoot();
        const auto requested = resolveManagedPath(path, false);
        rejectUnsafeExistingPathComponents(requested);
        rejectProtectedPath(requested);
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
        if (!std::filesystem::is_directory(resolved, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }
        if (!recursive && !std::filesystem::is_empty(resolved, error)) {
            if (error) {
                throw storageError();
            }
            throw BackendException(ErrorCode::Conflict, "Directory is not empty.");
        }

        if (recursive) {
            std::vector<std::filesystem::path> deletionOrder;
            const auto collectTree = [&](const auto& self, const std::filesystem::path& directory) -> void {
                rejectUnsafeExistingPathComponents(directory);
                rejectProtectedPath(directory);
                if (isReparseOrSymlink(directory)) {
                    throw permissionDenied();
                }

                std::error_code walkError;
                for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                    const auto child = entry.path();
                    rejectUnsafeExistingPathComponents(child);
                    rejectProtectedPath(child);
                    if (isReparseOrSymlink(child)) {
                        throw permissionDenied();
                    }

                    const auto status = entry.symlink_status(walkError);
                    if (walkError) {
                        throw storageError();
                    }
                    if (std::filesystem::is_directory(status)) {
                        self(self, child);
                    } else {
                        deletionOrder.push_back(child);
                    }
                }

                rejectUnsafeExistingPathComponents(directory);
                rejectProtectedPath(directory);
                if (isReparseOrSymlink(directory)) {
                    throw permissionDenied();
                }
                deletionOrder.push_back(directory);
            };
            collectTree(collectTree, resolved);

            for (const auto& target : deletionOrder) {
                rejectUnsafeExistingPathComponents(target);
                rejectProtectedPath(target);
                if (isReparseOrSymlink(target)) {
                    throw permissionDenied();
                }
            }

            for (const auto& target : deletionOrder) {
                std::filesystem::remove(target, error);
                if (error) {
                    throw storageError();
                }
            }
        } else if (!std::filesystem::remove(resolved, error) || error) {
            throw storageError();
        }
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

std::vector<FileEntry> FilesystemFileStore::search(const std::string& root, const std::string& query) const {
    try {
        ensureTrustedRoot();
        const auto requested = resolveManagedPath(root, true);
        rejectUnsafeExistingPathComponents(requested);
        rejectProtectedPath(requested);
        const auto resolved = canonicalExistingPath(requested);
        if (!isPathInside(root_, resolved)) {
            throw invalidPath();
        }
        if (!root.empty()) {
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

        std::vector<FileEntry> results;
        const auto loweredQuery = lowerAscii(query);
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator iterator(resolved, options, error);
        if (error) {
            throw storageError();
        }
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }

            const auto& entryPath = iterator->path();
            const auto name = entryPath.filename().string();
            if (name == "temp" || isReparseOrSymlink(entryPath)) {
                if (std::filesystem::is_directory(iterator->symlink_status(error))) {
                    iterator.disable_recursion_pending();
                }
                error.clear();
                continue;
            }

            const auto relative = entryPath.lexically_relative(root_);
            if (isProtectedRelativePath(relative)) {
                if (std::filesystem::is_directory(iterator->symlink_status(error))) {
                    iterator.disable_recursion_pending();
                }
                error.clear();
                continue;
            }

            if (lowerAscii(name).find(loweredQuery) != std::string::npos) {
                results.push_back(entryFromPath(entryPath));
            }
        }
        std::sort(results.begin(), results.end(), [](const FileEntry& left, const FileEntry& right) {
            const auto leftPath = lowerAscii(left.path);
            const auto rightPath = lowerAscii(right.path);
            if (leftPath != rightPath) {
                return leftPath < rightPath;
            }
            return left.path < right.path;
        });
        return results;
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

    if (isProtectedRelativePath(relative)) {
        throw permissionDenied();
    }
}

void FilesystemFileStore::rejectSamePath(const std::filesystem::path& from, const std::filesystem::path& to) const {
    if (stableAbsolutePath(from) == stableAbsolutePath(to)) {
        throw BackendException(ErrorCode::Conflict, "Source and destination are the same.");
    }
}

void FilesystemFileStore::rejectExistingDestination(const std::filesystem::path& destination, bool overwrite) const {
    std::error_code error;
    if (std::filesystem::exists(destination, error) && !overwrite) {
        throw BackendException(ErrorCode::AlreadyExists, "Destination already exists.");
    }
}

FileEntry FilesystemFileStore::entryFromPath(const std::filesystem::path& path) const {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        throw BackendException(ErrorCode::StorageError, "File storage error.");
    }
    FileEntry entry;
    entry.name = path.filename().string();
    entry.path = std::filesystem::relative(path, root_, error).generic_string();
    if (error) {
        throw BackendException(ErrorCode::StorageError, "File storage error.");
    }
    entry.type = std::filesystem::is_directory(status) ? FileEntryType::Directory : FileEntryType::File;
    if (std::filesystem::is_regular_file(status)) {
        entry.size = std::filesystem::file_size(path, error);
        if (error) {
            entry.size = 0;
        }
    }
    return entry;
}

} // namespace tundraux::backend

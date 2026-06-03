#include "filesystem_tux_store.hpp"

#include "crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tundraux::backend {

namespace {

constexpr unsigned int kTuxFormatVersion = 1;
constexpr std::size_t kMaxMetadataStringLength = 1024;
constexpr std::size_t kMaxContentLength = 16u * 1024u * 1024u;
constexpr const char* kInvalidPathMessage = "Invalid path.";
constexpr const char* kNotFoundMessage = "File not found.";
constexpr const char* kAlreadyExistsMessage = "Destination already exists.";
constexpr const char* kAccessDeniedMessage = "Access denied.";
constexpr const char* kStorageErrorMessage = "TUX storage error.";
constexpr const char* kCorruptTuxMessage = "TUX file is corrupt or unsupported.";

BackendException invalidPath() {
    return BackendException(ErrorCode::InvalidPath, kInvalidPathMessage);
}

BackendException notFound() {
    return BackendException(ErrorCode::NotFound, kNotFoundMessage);
}

BackendException alreadyExists() {
    return BackendException(ErrorCode::AlreadyExists, kAlreadyExistsMessage);
}

BackendException permissionDenied() {
    return BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
}

BackendException storageError() {
    return BackendException(ErrorCode::StorageError, kStorageErrorMessage);
}

BackendException corruptTux() {
    return BackendException(ErrorCode::StorageError, kCorruptTuxMessage);
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

bool isValidComponent(const std::string& component) {
    return !component.empty() && std::all_of(component.begin(), component.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_';
    });
}

bool isReservedDosDeviceName(const std::string& component) {
    const auto lower = lowerAscii(component);
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul") {
        return true;
    }
    if (lower.size() == 4 &&
        (lower.rfind("com", 0) == 0 || lower.rfind("lpt", 0) == 0) &&
        lower[3] >= '1' && lower[3] <= '9') {
        return true;
    }
    return false;
}

std::vector<std::string> splitApiPath(const std::string& path, bool allowRoot) {
    if (path.empty()) {
        if (allowRoot) {
            return {};
        }
        throw invalidPath();
    }
    if (path.find('\\') != std::string::npos) {
        throw invalidPath();
    }
    const std::filesystem::path requested(path);
    if (requested.is_absolute()) {
        throw invalidPath();
    }

    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (part == "." || part == ".." || !isValidComponent(part) || isReservedDosDeviceName(part)) {
            throw invalidPath();
        }
        parts.push_back(part);
    }
    if (parts.empty() && !allowRoot) {
        throw invalidPath();
    }
    for (const auto& component : parts) {
        if (lowerAscii(component) == "temp") {
            throw invalidPath();
        }
    }
    return parts;
}

std::string lexicalEntryPath(const std::filesystem::path& root, const std::filesystem::path& entry) {
    std::error_code error;
    auto relative = std::filesystem::relative(entry, root, error);
    if (error) {
        relative = entry.lexically_relative(root);
    }
    return relative.generic_string();
}

std::string stripTuxExtension(std::string path) {
    constexpr std::size_t extensionLength = 4;
    if (path.size() >= extensionLength && lowerAscii(path.substr(path.size() - extensionLength)) == ".tux") {
        path.resize(path.size() - extensionLength);
    }
    return path;
}

bool isTuxFile(const std::filesystem::directory_entry& entry) {
    std::error_code error;
    return !isReparseOrSymlink(entry.path()) &&
        entry.is_regular_file(error) && !error && entry.path().extension() == ".TUX";
}

void readExact(std::ifstream& in, void* destination, std::size_t size) {
    in.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
    if (!in) {
        throw corruptTux();
    }
}

std::string readEncryptedString(std::ifstream& in, std::uintmax_t& remaining, std::size_t maxLength) {
    size_t length = 0;
    if (remaining < sizeof(length)) {
        throw corruptTux();
    }
    readExact(in, &length, sizeof(length));
    remaining -= sizeof(length);
    if (length > maxLength || length > remaining) {
        throw corruptTux();
    }
    std::string encrypted(length, '\0');
    if (length > 0) {
        readExact(in, encrypted.data(), length);
    }
    remaining -= length;
    return encryptDecrypt(encrypted);
}

void writeEncryptedString(std::ofstream& out, const std::string& value, std::size_t maxLength) {
    if (value.size() > maxLength) {
        throw storageError();
    }
    const std::string encrypted = encryptDecrypt(value);
    const size_t length = encrypted.size();
    out.write(reinterpret_cast<const char*>(&length), sizeof(length));
    out.write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
    if (!out) {
        throw storageError();
    }
}

TuxContent readTuxFile(const std::filesystem::path& path, bool includeContent) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error) {
            throw storageError();
        }
        throw notFound();
    }
    if (!std::filesystem::is_regular_file(path, error)) {
        if (error) {
            throw storageError();
        }
        throw invalidPath();
    }
    auto remaining = std::filesystem::file_size(path, error);
    if (error) {
        throw storageError();
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw storageError();
    }

    unsigned int version = 0;
    if (remaining < sizeof(version)) {
        throw corruptTux();
    }
    readExact(in, &version, sizeof(version));
    remaining -= sizeof(version);
    if (version != kTuxFormatVersion) {
        throw corruptTux();
    }

    TuxContent result;
    result.metadata.creator = readEncryptedString(in, remaining, kMaxMetadataStringLength);
    result.metadata.lastEditor = readEncryptedString(in, remaining, kMaxMetadataStringLength);
    if (remaining < sizeof(result.metadata.createTime) + sizeof(result.metadata.modifyTime)) {
        throw corruptTux();
    }
    readExact(in, &result.metadata.createTime, sizeof(result.metadata.createTime));
    remaining -= sizeof(result.metadata.createTime);
    readExact(in, &result.metadata.modifyTime, sizeof(result.metadata.modifyTime));
    remaining -= sizeof(result.metadata.modifyTime);

    if (includeContent) {
        result.content = readEncryptedString(in, remaining, kMaxContentLength);
    }
    return result;
}

void replaceFile(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(
            from.wstring().c_str(),
            to.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw storageError();
    }
#else
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        throw storageError();
    }
#endif
}

void moveFileReplacingDestination(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(
            from.wstring().c_str(),
            to.wstring().c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw storageError();
    }
#else
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        throw storageError();
    }
#endif
}

std::filesystem::path tempPathFor(const std::filesystem::path& destination) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return destination.parent_path() /
        (destination.filename().string() + "." + std::to_string(ticks) + "." + std::to_string(threadId) + ".tmp");
}

void writeTuxFile(const std::filesystem::path& path, const std::string& content, const TuxMetadata& metadata) {
    if (content.size() > kMaxContentLength) {
        throw storageError();
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        throw storageError();
    }

    const auto temporary = tempPathFor(path);
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw storageError();
        }
        out.write(reinterpret_cast<const char*>(&kTuxFormatVersion), sizeof(kTuxFormatVersion));
        writeEncryptedString(out, metadata.creator, kMaxMetadataStringLength);
        writeEncryptedString(out, metadata.lastEditor, kMaxMetadataStringLength);
        out.write(reinterpret_cast<const char*>(&metadata.createTime), sizeof(metadata.createTime));
        out.write(reinterpret_cast<const char*>(&metadata.modifyTime), sizeof(metadata.modifyTime));
        writeEncryptedString(out, content, kMaxContentLength);
        out.flush();
        if (!out) {
            std::filesystem::remove(temporary, error);
            throw storageError();
        }
    }

    try {
        replaceFile(temporary, path);
    } catch (...) {
        std::filesystem::remove(temporary, error);
        throw;
    }
}

void rejectExistingDestination(const std::filesystem::path& destination, bool overwrite) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (!overwrite) {
            throw alreadyExists();
        }
        if (!std::filesystem::is_regular_file(destination, error)) {
            throw invalidPath();
        }
    } else if (error) {
        throw storageError();
    }
}

void rejectSamePath(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (stableAbsolutePath(from) == stableAbsolutePath(to)) {
        throw BackendException(ErrorCode::Conflict, "Source and destination are the same.");
    }
}

} // namespace

FilesystemTuxStore::FilesystemTuxStore(std::string root)
    : configuredRoot_(std::filesystem::absolute(std::filesystem::path(std::move(root))).lexically_normal()),
      root_(stableAbsolutePath(configuredRoot_)) {
    ensureTrustedRoot();
}

std::vector<FileEntry> FilesystemTuxStore::list(const std::string& path) const {
    try {
        ensureTrustedRoot();
        const auto directory = resolveDirectoryPath(path, true);
        rejectUnsafeExistingPathComponents(directory);
        std::error_code error;
        if (!std::filesystem::exists(directory, error)) {
            if (error) {
                throw storageError();
            }
            throw notFound();
        }
        if (!std::filesystem::is_directory(directory, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }

        std::vector<FileEntry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const auto name = entry.path().filename().string();
            if (lowerAscii(name) == "temp") {
                continue;
            }
            if (isReparseOrSymlink(entry.path())) {
                continue;
            }
            std::error_code statusError;
            if (entry.is_directory(statusError) && !statusError) {
                entries.push_back(entryFromPath(entry.path()));
            } else if (isTuxFile(entry)) {
                entries.push_back(entryFromPath(entry.path()));
            }
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

TuxMetadata FilesystemTuxStore::metadata(const std::string& path) const {
    try {
        ensureTrustedRoot();
        const auto target = resolveFilePath(path);
        rejectUnsafeExistingPathComponents(target);
        return readTuxFile(target, false).metadata;
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

TuxContent FilesystemTuxStore::read(const std::string& path) const {
    try {
        ensureTrustedRoot();
        const auto target = resolveFilePath(path);
        rejectUnsafeExistingPathComponents(target);
        return readTuxFile(target, true);
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemTuxStore::create(const std::string& path, const TuxMetadata& metadata, bool overwrite) {
    try {
        ensureTrustedRoot();
        const auto destination = resolveFilePath(path);
        rejectUnsafeExistingPathComponents(destination);
        rejectExistingDestination(destination, overwrite);
        writeTuxFile(destination, "", metadata);
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemTuxStore::write(const std::string& path, const std::string& content, const TuxMetadata& metadata) {
    try {
        ensureTrustedRoot();
        const auto destination = resolveFilePath(path);
        rejectUnsafeExistingPathComponents(destination);
        writeTuxFile(destination, content, metadata);
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemTuxStore::deleteFile(const std::string& path) {
    try {
        ensureTrustedRoot();
        const auto target = resolveFilePath(path);
        rejectUnsafeExistingPathComponents(target);
        std::error_code error;
        if (!std::filesystem::exists(target, error)) {
            if (error) {
                throw storageError();
            }
            throw notFound();
        }
        if (!std::filesystem::is_regular_file(target, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }
        if (!std::filesystem::remove(target, error) || error) {
            throw storageError();
        }
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemTuxStore::renameFile(const std::string& from, const std::string& to, bool overwrite) {
    moveFile(from, to, overwrite);
}

void FilesystemTuxStore::copyFile(const std::string& from, const std::string& to, const TuxMetadata& metadata, bool overwrite) {
    try {
        ensureTrustedRoot();
        const auto source = resolveFilePath(from);
        const auto destination = resolveFilePath(to);
        rejectUnsafeExistingPathComponents(source);
        rejectUnsafeExistingPathComponents(destination);
        rejectSamePath(source, destination);
        rejectExistingDestination(destination, overwrite);
        const auto sourceContent = readTuxFile(source, true);
        writeTuxFile(destination, sourceContent.content, metadata);
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

void FilesystemTuxStore::moveFile(const std::string& from, const std::string& to, bool overwrite) {
    try {
        ensureTrustedRoot();
        const auto source = resolveFilePath(from);
        const auto destination = resolveFilePath(to);
        rejectUnsafeExistingPathComponents(source);
        rejectUnsafeExistingPathComponents(destination);
        rejectSamePath(source, destination);
        rejectExistingDestination(destination, overwrite);
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
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            throw storageError();
        }
        if (overwrite) {
            moveFileReplacingDestination(source, destination);
        } else {
            std::filesystem::rename(source, destination, error);
            if (error) {
                throw storageError();
            }
        }
    } catch (const BackendException&) {
        throw;
    } catch (const std::exception&) {
        throw storageError();
    }
}

std::vector<FileEntry> FilesystemTuxStore::search(const std::string& root, const std::string& query) const {
    try {
        ensureTrustedRoot();
        const auto directory = resolveDirectoryPath(root, true);
        rejectUnsafeExistingPathComponents(directory);
        std::error_code error;
        if (!std::filesystem::exists(directory, error)) {
            if (error) {
                throw storageError();
            }
            throw notFound();
        }
        if (!std::filesystem::is_directory(directory, error)) {
            if (error) {
                throw storageError();
            }
            throw invalidPath();
        }

        std::vector<FileEntry> results;
        const auto loweredQuery = lowerAscii(query);
        std::filesystem::recursive_directory_iterator iterator(
            directory,
            std::filesystem::directory_options::skip_permission_denied,
            error
        );
        if (error) {
            throw storageError();
        }
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (isReparseOrSymlink(iterator->path())) {
                if (iterator->is_directory(error)) {
                    iterator.disable_recursion_pending();
                }
                error.clear();
                continue;
            }
            if (lowerAscii(iterator->path().filename().string()) == "temp" && iterator->is_directory(error)) {
                iterator.disable_recursion_pending();
                error.clear();
                continue;
            }
            if (!isTuxFile(*iterator)) {
                continue;
            }
            const auto stem = iterator->path().stem().string();
            if (lowerAscii(stem).find(loweredQuery) != std::string::npos) {
                results.push_back(entryFromPath(iterator->path()));
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

void FilesystemTuxStore::ensureTrustedRoot() const {
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
    const auto resolvedRoot = stableAbsolutePath(configuredRoot_);
    if (resolvedRoot != root_) {
        throw permissionDenied();
    }
}

void FilesystemTuxStore::rejectUnsafeExistingPathComponents(const std::filesystem::path& resolved) const {
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
        std::error_code error;
        if (!std::filesystem::exists(current, error)) {
            return;
        }
        if (error) {
            throw storageError();
        }
    }
}

std::filesystem::path FilesystemTuxStore::resolveDirectoryPath(const std::string& path, bool allowRoot) const {
    auto resolved = root_;
    for (const auto& part : splitApiPath(path, allowRoot)) {
        resolved /= part;
    }
    resolved = resolved.lexically_normal();
    if (!isPathInside(root_, resolved)) {
        throw invalidPath();
    }
    return resolved;
}

std::filesystem::path FilesystemTuxStore::resolveFilePath(const std::string& path) const {
    auto resolved = resolveDirectoryPath(path, false);
    resolved += ".TUX";
    resolved = resolved.lexically_normal();
    if (!isPathInside(root_, resolved)) {
        throw invalidPath();
    }
    return resolved;
}

FileEntry FilesystemTuxStore::entryFromPath(const std::filesystem::path& path) const {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        throw storageError();
    }
    FileEntry entry;
    if (std::filesystem::is_directory(status)) {
        entry.name = path.filename().string();
        entry.path = lexicalEntryPath(root_, path);
        entry.type = FileEntryType::Directory;
    } else {
        entry.name = path.stem().string();
        entry.path = stripTuxExtension(lexicalEntryPath(root_, path));
        entry.type = FileEntryType::File;
        entry.size = std::filesystem::file_size(path, error);
        if (error) {
            entry.size = 0;
        }
    }
    return entry;
}

} // namespace tundraux::backend

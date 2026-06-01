#include "file_service.hpp"

#include <algorithm>
#include <cctype>
#include <exception>

namespace tundraux::backend {

namespace {

constexpr const char* kAccessDeniedMessage = "Access denied.";
constexpr const char* kFileStorageErrorMessage = "File storage error.";
constexpr const char* kReadUserDataError = "Unable to read user data.";

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isSyntheticDebugUser(const BackendUser& user) {
    return lowerAscii(user.type) == "debug" && user.name == "debug" && user.password.empty();
}

template <typename Func>
ServiceResult<EmptyResult> runFileMutation(Func func) {
    try {
        func();
        return ServiceResult<EmptyResult>::success(EmptyResult{});
    } catch (const BackendException& error) {
        return ServiceResult<EmptyResult>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    } catch (...) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    }
}

} // namespace

FileService::FileService(FileStore& files, const SessionService& sessions, const UserStore& users)
    : files_(files), sessions_(sessions), users_(users) {}

ServiceResult<BackendUser> FileService::requireFileAccess(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<BackendUser>::failure(session.error.code, session.error.message);
    }
    if (session.value.type == "guest" || session.value.name.empty()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    if (isSyntheticDebugUser(session.value)) {
        return ServiceResult<BackendUser>::success(session.value);
    }

    std::vector<BackendUser> users;
    try {
        users = users_.listUsers();
    } catch (const std::exception&) {
        return ServiceResult<BackendUser>::failure(ErrorCode::StorageError, kReadUserDataError);
    }

    const auto found = std::find_if(users.begin(), users.end(), [&](const BackendUser& user) {
        return user.name == session.value.name;
    });
    if (found == users.end()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::NotFound, "User not found.");
    }
    if (found->type == "guest" || found->name.empty() || found->failedCount > 7) {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    return ServiceResult<BackendUser>::success(*found);
}

ServiceResult<std::vector<FileEntry>> FileService::listDirectory(
    const std::string& sessionId,
    const std::string& path
) const {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<std::vector<FileEntry>>::failure(access.error.code, access.error.message);
    }

    try {
        return ServiceResult<std::vector<FileEntry>>::success(files_.listDirectory(path));
    } catch (const BackendException& error) {
        return ServiceResult<std::vector<FileEntry>>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    }
}

ServiceResult<FileContent> FileService::readFile(
    const std::string& sessionId,
    const std::string& path
) const {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<FileContent>::failure(access.error.code, access.error.message);
    }

    try {
        return ServiceResult<FileContent>::success(files_.readFile(path));
    } catch (const BackendException& error) {
        return ServiceResult<FileContent>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<FileContent>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    }
}

ServiceResult<EmptyResult> FileService::writeFile(
    const std::string& sessionId,
    const std::string& path,
    const std::string& content
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    try {
        files_.writeFile(path, content);
        return ServiceResult<EmptyResult>::success(EmptyResult{});
    } catch (const BackendException& error) {
        return ServiceResult<EmptyResult>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    }
}

ServiceResult<EmptyResult> FileService::deleteFile(const std::string& sessionId, const std::string& path) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runFileMutation([this, &path]() {
        files_.deleteFile(path);
    });
}

ServiceResult<EmptyResult> FileService::renameFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runFileMutation([this, &from, &to, overwrite]() {
        files_.renameFile(from, to, overwrite);
    });
}

ServiceResult<EmptyResult> FileService::copyFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runFileMutation([this, &from, &to, overwrite]() {
        files_.copyFile(from, to, overwrite);
    });
}

ServiceResult<EmptyResult> FileService::moveFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runFileMutation([this, &from, &to, overwrite]() {
        files_.moveFile(from, to, overwrite);
    });
}

ServiceResult<EmptyResult> FileService::createDirectory(const std::string& sessionId, const std::string& path) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runFileMutation([this, &path]() {
        files_.createDirectory(path);
    });
}

ServiceResult<EmptyResult> FileService::removeDirectory(
    const std::string& sessionId,
    const std::string& path,
    bool recursive
) {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runFileMutation([this, &path, recursive]() {
        files_.removeDirectory(path, recursive);
    });
}

ServiceResult<std::vector<FileEntry>> FileService::search(
    const std::string& sessionId,
    const std::string& root,
    const std::string& query
) const {
    const auto access = requireFileAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<std::vector<FileEntry>>::failure(access.error.code, access.error.message);
    }

    try {
        return ServiceResult<std::vector<FileEntry>>::success(files_.search(root, query));
    } catch (const BackendException& error) {
        return ServiceResult<std::vector<FileEntry>>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    } catch (...) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kFileStorageErrorMessage);
    }
}

} // namespace tundraux::backend

#include "tux_service.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>

namespace tundraux::backend {

namespace {

constexpr const char* kAccessDeniedMessage = "Access denied.";
constexpr const char* kTuxStorageErrorMessage = "TUX storage error.";

bool isPrivileged(const BackendUser& user) {
    std::string type = user.type;
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return type == "admin" || type == "debug";
}

std::time_t currentTime() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

template <typename Func>
ServiceResult<EmptyResult> runTuxMutation(Func func) {
    try {
        func();
        return ServiceResult<EmptyResult>::success(EmptyResult{});
    } catch (const BackendException& error) {
        return ServiceResult<EmptyResult>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    } catch (...) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    }
}

} // namespace

TuxService::TuxService(TuxStore& store, const SessionService& sessions)
    : store_(store), sessions_(sessions) {}

ServiceResult<BackendUser> TuxService::requireTuxAccess(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<BackendUser>::failure(session.error.code, session.error.message);
    }
    if (session.value.type == "guest") {
        return ServiceResult<BackendUser>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
    }
    return session;
}

bool TuxService::canAccess(const BackendUser& user, const TuxMetadata& metadata) const {
    return isPrivileged(user) || (!user.name.empty() && metadata.creator == user.name);
}

TuxMetadata TuxService::newMetadata(const BackendUser& user) const {
    const auto now = currentTime();
    return TuxMetadata{user.name, user.name, now, now};
}

TuxMetadata TuxService::updatedMetadata(const BackendUser& user, TuxMetadata metadata) const {
    metadata.lastEditor = user.name;
    metadata.modifyTime = currentTime();
    return metadata;
}

ServiceResult<std::vector<FileEntry>> TuxService::list(const std::string& sessionId, const std::string& path) const {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<std::vector<FileEntry>>::failure(access.error.code, access.error.message);
    }

    try {
        return ServiceResult<std::vector<FileEntry>>::success(store_.list(path));
    } catch (const BackendException& error) {
        return ServiceResult<std::vector<FileEntry>>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    } catch (...) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    }
}

ServiceResult<TuxContent> TuxService::read(const std::string& sessionId, const std::string& path) const {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<TuxContent>::failure(access.error.code, access.error.message);
    }

    try {
        const auto content = store_.read(path);
        if (!canAccess(access.value, content.metadata)) {
            return ServiceResult<TuxContent>::failure(ErrorCode::PermissionDenied, kAccessDeniedMessage);
        }
        return ServiceResult<TuxContent>::success(content);
    } catch (const BackendException& error) {
        return ServiceResult<TuxContent>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<TuxContent>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    } catch (...) {
        return ServiceResult<TuxContent>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    }
}

ServiceResult<EmptyResult> TuxService::create(const std::string& sessionId, const std::string& path, bool overwrite) {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runTuxMutation([this, &access, &path, overwrite]() {
        if (overwrite) {
            try {
                const auto existing = store_.metadata(path);
                if (!canAccess(access.value, existing)) {
                    throw BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
                }
            } catch (const BackendException& error) {
                if (error.code() != ErrorCode::NotFound) {
                    throw;
                }
            }
        }
        store_.create(path, newMetadata(access.value), overwrite);
    });
}

ServiceResult<EmptyResult> TuxService::write(const std::string& sessionId, const std::string& path, const std::string& content) {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runTuxMutation([this, &access, &path, &content]() {
        const auto existing = store_.metadata(path);
        if (!canAccess(access.value, existing)) {
            throw BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
        }
        store_.write(path, content, updatedMetadata(access.value, existing));
    });
}

ServiceResult<EmptyResult> TuxService::deleteFile(const std::string& sessionId, const std::string& path) {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runTuxMutation([this, &access, &path]() {
        const auto existing = store_.metadata(path);
        if (!canAccess(access.value, existing)) {
            throw BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
        }
        store_.deleteFile(path);
    });
}

ServiceResult<EmptyResult> TuxService::renameFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runTuxMutation([this, &access, &from, &to, overwrite]() {
        const auto existing = store_.metadata(from);
        if (!canAccess(access.value, existing)) {
            throw BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
        }
        store_.renameFile(from, to, overwrite);
    });
}

ServiceResult<EmptyResult> TuxService::copyFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runTuxMutation([this, &access, &from, &to, overwrite]() {
        const auto source = store_.read(from);
        if (!canAccess(access.value, source.metadata)) {
            throw BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
        }
        store_.copyFile(from, to, newMetadata(access.value), overwrite);
    });
}

ServiceResult<EmptyResult> TuxService::moveFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<EmptyResult>::failure(access.error.code, access.error.message);
    }

    return runTuxMutation([this, &access, &from, &to, overwrite]() {
        const auto existing = store_.metadata(from);
        if (!canAccess(access.value, existing)) {
            throw BackendException(ErrorCode::PermissionDenied, kAccessDeniedMessage);
        }
        store_.moveFile(from, to, overwrite);
    });
}

ServiceResult<std::vector<FileEntry>> TuxService::search(
    const std::string& sessionId,
    const std::string& root,
    const std::string& query
) const {
    const auto access = requireTuxAccess(sessionId);
    if (!access.ok) {
        return ServiceResult<std::vector<FileEntry>>::failure(access.error.code, access.error.message);
    }

    try {
        return ServiceResult<std::vector<FileEntry>>::success(store_.search(root, query));
    } catch (const BackendException& error) {
        return ServiceResult<std::vector<FileEntry>>::failure(error.code(), error.what());
    } catch (const std::exception&) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    } catch (...) {
        return ServiceResult<std::vector<FileEntry>>::failure(ErrorCode::StorageError, kTuxStorageErrorMessage);
    }
}

} // namespace tundraux::backend

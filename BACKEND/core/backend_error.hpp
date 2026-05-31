#pragma once

#include <string>

namespace tundraux::backend {

enum class ErrorCode {
    InvalidRequest,
    UnknownMethod,
    InvalidParams,
    SessionExpired,
    AuthenticationFailed,
    PermissionDenied,
    InvalidPath,
    NotFound,
    AlreadyExists,
    Conflict,
    StorageError,
    InternalError
};

struct BackendError {
    ErrorCode code;
    std::string message;
};

inline const char* toString(ErrorCode code) {
    switch (code) {
    case ErrorCode::InvalidRequest: return "InvalidRequest";
    case ErrorCode::UnknownMethod: return "UnknownMethod";
    case ErrorCode::InvalidParams: return "InvalidParams";
    case ErrorCode::SessionExpired: return "SessionExpired";
    case ErrorCode::AuthenticationFailed: return "AuthenticationFailed";
    case ErrorCode::PermissionDenied: return "PermissionDenied";
    case ErrorCode::InvalidPath: return "InvalidPath";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::Conflict: return "Conflict";
    case ErrorCode::StorageError: return "StorageError";
    case ErrorCode::InternalError: return "InternalError";
    }
    return "InternalError";
}

} // namespace tundraux::backend

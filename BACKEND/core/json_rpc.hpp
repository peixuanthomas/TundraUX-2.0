#pragma once

#include "file_service.hpp"
#include "json.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <memory>
#include <string>

namespace tundraux::backend {

class JsonRpcDispatcher {
public:
    JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files);
    JsonRpcDispatcher(SessionService& sessions, UserService& users);

    std::string handleLine(const std::string& line);

private:
    SessionService& sessions_;
    UserService& users_;
    std::unique_ptr<FileStore> fallbackFileStore_;
    std::unique_ptr<FileService> fallbackFiles_;
    FileService& files_;

    JsonValue dispatch(const std::string& method, const JsonValue::Object& params);
    JsonValue errorResponse(const JsonValue& id, ErrorCode code, const std::string& message) const;
    JsonValue successResponse(const JsonValue& id, JsonValue result) const;
};

} // namespace tundraux::backend

#pragma once

#include "json.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <string>

namespace tundraux::backend {

class JsonRpcDispatcher {
public:
    JsonRpcDispatcher(SessionService& sessions, UserService& users);

    std::string handleLine(const std::string& line);

private:
    SessionService& sessions_;
    UserService& users_;

    JsonValue dispatch(const std::string& method, const JsonValue::Object& params);
    JsonValue errorResponse(const JsonValue& id, ErrorCode code, const std::string& message) const;
    JsonValue successResponse(const JsonValue& id, JsonValue result) const;
};

} // namespace tundraux::backend

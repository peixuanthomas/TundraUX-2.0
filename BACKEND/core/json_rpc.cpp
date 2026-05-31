#include "json_rpc.hpp"

#include <exception>
#include <string>
#include <utility>

namespace tundraux::backend {
namespace {

class RpcError final : public std::exception {
public:
    RpcError(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

    ErrorCode code() const {
        return code_;
    }

    const std::string& message() const {
        return message_;
    }

private:
    ErrorCode code_;
    std::string message_;
};

JsonValue userToJson(const BackendUser& user) {
    return JsonValue::object({
        {"name", JsonValue::string(user.name)},
        {"type", JsonValue::string(user.type)}
    });
}

JsonValue sessionToJson(const SessionInfo& session) {
    return JsonValue::object({
        {"sessionId", JsonValue::string(session.sessionId)},
        {"user", userToJson(session.user)}
    });
}

JsonValue fileEntryToJson(const FileEntry& entry) {
    return JsonValue::object({
        {"name", JsonValue::string(entry.name)},
        {"path", JsonValue::string(entry.path)},
        {"type", JsonValue::string(entry.type == FileEntryType::Directory ? "directory" : "file")},
        {"size", JsonValue::number(static_cast<double>(entry.size))}
    });
}

std::string requiredStringParam(const JsonValue::Object& params, const std::string& name) {
    const auto found = params.find(name);
    if (found == params.end() || found->second.type() != JsonValue::Type::String) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asString();
}

void throwIfFailed(const BackendError& error) {
    throw RpcError(error.code, error.message);
}

} // namespace

JsonRpcDispatcher::JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files)
    : sessions_(sessions), users_(users), files_(&files) {}

JsonRpcDispatcher::JsonRpcDispatcher(SessionService& sessions, UserService& users)
    : sessions_(sessions), users_(users) {}

std::string JsonRpcDispatcher::handleLine(const std::string& line) {
    JsonValue id = JsonValue::null();
    const auto parsed = parseJson(line);
    if (!parsed.ok) {
        return stringifyJson(errorResponse(id, ErrorCode::InvalidRequest, parsed.error.message));
    }

    try {
        if (parsed.value.type() != JsonValue::Type::Object) {
            return stringifyJson(errorResponse(id, ErrorCode::InvalidRequest, "Request must be an object."));
        }

        const auto& request = parsed.value.asObject();
        const auto idEntry = request.find("id");
        if (idEntry != request.end()) {
            if (idEntry->second.type() != JsonValue::Type::String) {
                return stringifyJson(errorResponse(JsonValue::null(), ErrorCode::InvalidRequest, "Request id must be a string."));
            }
            id = idEntry->second;
        }

        const auto methodEntry = request.find("method");
        if (methodEntry == request.end() || methodEntry->second.type() != JsonValue::Type::String) {
            return stringifyJson(errorResponse(id, ErrorCode::InvalidRequest, "Request method must be a string."));
        }

        JsonValue::Object emptyParams;
        const JsonValue::Object* params = &emptyParams;
        const auto paramsEntry = request.find("params");
        if (paramsEntry != request.end()) {
            if (paramsEntry->second.type() != JsonValue::Type::Object) {
                return stringifyJson(errorResponse(id, ErrorCode::InvalidParams, "Request params must be an object."));
            }
            params = &paramsEntry->second.asObject();
        }

        return stringifyJson(successResponse(id, dispatch(methodEntry->second.asString(), *params)));
    } catch (const RpcError& error) {
        return stringifyJson(errorResponse(id, error.code(), error.message()));
    } catch (const std::exception&) {
        return stringifyJson(errorResponse(id, ErrorCode::InternalError, "Internal error."));
    }
}

JsonValue JsonRpcDispatcher::dispatch(const std::string& method, const JsonValue::Object& params) {
    if (method == "session.startGuestSession") {
        return sessionToJson(sessions_.startGuestSession());
    }

    if (method == "session.login") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string username = requiredStringParam(params, "username");
        const std::string password = requiredStringParam(params, "password");
        const auto result = sessions_.login(sessionId, username, password);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return sessionToJson(result.value);
    }

    if (method == "session.logout") {
        const auto result = sessions_.logout(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (method == "session.whoami") {
        const auto result = sessions_.whoami(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"user", userToJson(result.value)}});
    }

    if (method == "user.listUsers") {
        const auto result = users_.listUsers(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }

        JsonValue::Array jsonUsers;
        for (const auto& user : result.value) {
            jsonUsers.push_back(userToJson(user));
        }
        return JsonValue::object({{"users", JsonValue::array(std::move(jsonUsers))}});
    }

    if (files_ != nullptr && method == "file.listDirectory") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->listDirectory(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }

        JsonValue::Array entries;
        for (const auto& entry : result.value) {
            entries.push_back(fileEntryToJson(entry));
        }
        return JsonValue::object({{"entries", JsonValue::array(std::move(entries))}});
    }

    if (files_ != nullptr && method == "file.readFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->readFile(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"content", JsonValue::string(result.value.content)}});
    }

    if (files_ != nullptr && method == "file.writeFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const std::string content = requiredStringParam(params, "content");
        const auto result = files_->writeFile(sessionId, path, content);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    throw RpcError(ErrorCode::UnknownMethod, "Unknown method.");
}

JsonValue JsonRpcDispatcher::errorResponse(const JsonValue& id, ErrorCode code, const std::string& message) const {
    return JsonValue::object({
        {"error", JsonValue::object({
            {"code", JsonValue::string(toString(code))},
            {"message", JsonValue::string(message)}
        })},
        {"id", id}
    });
}

JsonValue JsonRpcDispatcher::successResponse(const JsonValue& id, JsonValue result) const {
    return JsonValue::object({
        {"id", id},
        {"result", std::move(result)}
    });
}

} // namespace tundraux::backend

#include "json_rpc.hpp"

#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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

protocol::JsonValue userToJson(const BackendUser& user) {
    return protocol::JsonValue::object({
        {"name", protocol::JsonValue::string(user.name)},
        {"type", protocol::JsonValue::string(user.type)},
        {"passwordHint", protocol::JsonValue::string(user.passwordHint)},
        {"failedCount", protocol::JsonValue::number(static_cast<double>(user.failedCount))}
    });
}

protocol::JsonValue sessionToJson(const SessionInfo& session) {
    return protocol::JsonValue::object({
        {"sessionId", protocol::JsonValue::string(session.sessionId)},
        {"user", userToJson(session.user)}
    });
}

protocol::JsonValue fileEntryToJson(const FileEntry& entry) {
    return protocol::JsonValue::object({
        {"name", protocol::JsonValue::string(entry.name)},
        {"path", protocol::JsonValue::string(entry.path)},
        {"type", protocol::JsonValue::string(entry.type == FileEntryType::Directory ? "directory" : "file")},
        {"size", protocol::JsonValue::number(static_cast<double>(entry.size))}
    });
}

std::string requiredStringParam(const protocol::JsonValue::Object& params, const std::string& name) {
    const auto found = params.find(name);
    if (found == params.end() || found->second.type() != protocol::JsonValue::Type::String) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asString();
}

bool optionalBoolParam(const protocol::JsonValue::Object& params, const std::string& name, bool defaultValue = false) {
    const auto found = params.find(name);
    if (found == params.end()) {
        return defaultValue;
    }
    if (found->second.type() != protocol::JsonValue::Type::Boolean) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asBoolean();
}

bool requiredBoolParam(const protocol::JsonValue::Object& params, const std::string& name) {
    const auto found = params.find(name);
    if (found == params.end() || found->second.type() != protocol::JsonValue::Type::Boolean) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asBoolean();
}

std::string optionalStringParam(const protocol::JsonValue::Object& params, const std::string& name, const std::string& defaultValue = "") {
    const auto found = params.find(name);
    if (found == params.end()) {
        return defaultValue;
    }
    if (found->second.type() != protocol::JsonValue::Type::String) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asString();
}

int optionalIntParam(const protocol::JsonValue::Object& params, const std::string& name, int defaultValue = 0) {
    const auto found = params.find(name);
    if (found == params.end()) {
        return defaultValue;
    }
    if (found->second.type() != protocol::JsonValue::Type::Number) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    const double number = found->second.asNumber();
    if (number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max()) ||
        number != static_cast<double>(static_cast<int>(number))) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return static_cast<int>(number);
}

const protocol::JsonValue::Object& requiredObjectParam(const protocol::JsonValue::Object& params, const std::string& name) {
    const auto found = params.find(name);
    if (found == params.end() || found->second.type() != protocol::JsonValue::Type::Object) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asObject();
}

BackendUser userFromJson(const protocol::JsonValue::Object& object, bool requirePassword) {
    BackendUser user;
    user.type = requiredStringParam(object, "type");
    user.name = requiredStringParam(object, "name");
    user.password = requirePassword
        ? requiredStringParam(object, "password")
        : optionalStringParam(object, "password");
    user.passwordHint = optionalStringParam(object, "passwordHint");
    user.failedCount = optionalIntParam(object, "failedCount");
    return user;
}

void throwIfFailed(const BackendError& error) {
    throw RpcError(error.code, error.message);
}

protocol::JsonValue entriesToJson(const std::vector<FileEntry>& value) {
    protocol::JsonValue::Array entries;
    for (const auto& entry : value) {
        entries.push_back(fileEntryToJson(entry));
    }
    return protocol::JsonValue::object({{"entries", protocol::JsonValue::array(std::move(entries))}});
}

} // namespace

JsonRpcDispatcher::JsonRpcDispatcher(
    SessionService& sessions,
    UserService& users,
    FileService& files,
    TuxService& tux,
    std::string debugSessionToken
) : sessions_(sessions),
    users_(users),
    files_(&files),
    tux_(&tux),
    debugSessionToken_(std::move(debugSessionToken)) {}

JsonRpcDispatcher::JsonRpcDispatcher(
    SessionService& sessions,
    UserService& users,
    FileService& files,
    std::string debugSessionToken
) : sessions_(sessions),
    users_(users),
    files_(&files),
    debugSessionToken_(std::move(debugSessionToken)) {}

JsonRpcDispatcher::JsonRpcDispatcher(
    SessionService& sessions,
    UserService& users,
    std::string debugSessionToken
) : sessions_(sessions),
    users_(users),
    debugSessionToken_(std::move(debugSessionToken)) {}

std::string JsonRpcDispatcher::handleLine(const std::string& line) {
    protocol::JsonValue id = protocol::JsonValue::null();
    const auto parsed = protocol::parseJson(line);
    if (!parsed.ok) {
        return protocol::stringifyJson(errorResponse(id, ErrorCode::InvalidRequest, parsed.error.message));
    }

    try {
        if (parsed.value.type() != protocol::JsonValue::Type::Object) {
            return protocol::stringifyJson(errorResponse(id, ErrorCode::InvalidRequest, "Request must be an object."));
        }

        const auto& request = parsed.value.asObject();
        const auto idEntry = request.find("id");
        if (idEntry != request.end()) {
            if (idEntry->second.type() != protocol::JsonValue::Type::String) {
                return protocol::stringifyJson(errorResponse(protocol::JsonValue::null(), ErrorCode::InvalidRequest, "Request id must be a string."));
            }
            id = idEntry->second;
        }

        const auto methodEntry = request.find("method");
        if (methodEntry == request.end() || methodEntry->second.type() != protocol::JsonValue::Type::String) {
            return protocol::stringifyJson(errorResponse(id, ErrorCode::InvalidRequest, "Request method must be a string."));
        }

        protocol::JsonValue::Object emptyParams;
        const protocol::JsonValue::Object* params = &emptyParams;
        const auto paramsEntry = request.find("params");
        if (paramsEntry != request.end()) {
            if (paramsEntry->second.type() != protocol::JsonValue::Type::Object) {
                return protocol::stringifyJson(errorResponse(id, ErrorCode::InvalidParams, "Request params must be an object."));
            }
            params = &paramsEntry->second.asObject();
        }

        return protocol::stringifyJson(successResponse(id, dispatch(methodEntry->second.asString(), *params)));
    } catch (const RpcError& error) {
        return protocol::stringifyJson(errorResponse(id, error.code(), error.message()));
    } catch (const std::exception&) {
        return protocol::stringifyJson(errorResponse(id, ErrorCode::InternalError, "Internal error."));
    }
}

protocol::JsonValue JsonRpcDispatcher::dispatch(const std::string& method, const protocol::JsonValue::Object& params) {
    if (method == "session.startGuestSession") {
        return sessionToJson(sessions_.startGuestSession());
    }

    if (method == "session.startDebugSession") {
        const std::string token = requiredStringParam(params, "token");
        if (debugSessionToken_.empty() || token != debugSessionToken_) {
            throw RpcError(ErrorCode::PermissionDenied, "Access Denied.");
        }
        return sessionToJson(sessions_.startSession(BackendUser{"debug", "debug", "", "", 0}));
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
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (method == "session.whoami") {
        const auto result = sessions_.whoami(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"user", userToJson(result.value)}});
    }

    if (method == "user.listUsers") {
        const auto result = users_.listUsers(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }

        protocol::JsonValue::Array jsonUsers;
        for (const auto& user : result.value) {
            jsonUsers.push_back(userToJson(user));
        }
        return protocol::JsonValue::object({{"users", protocol::JsonValue::array(std::move(jsonUsers))}});
    }

    if (method == "user.currentProfile") {
        const auto result = users_.currentProfile(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"user", userToJson(result.value)}});
    }

    if (method == "user.createUser") {
        const auto& user = requiredObjectParam(params, "user");
        const auto result = users_.createUser(
            requiredStringParam(params, "sessionId"),
            userFromJson(user, true)
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (method == "user.updateUser") {
        const auto& user = requiredObjectParam(params, "user");
        const bool passwordProvided = optionalBoolParam(params, "passwordProvided");
        const auto result = users_.updateUser(
            requiredStringParam(params, "sessionId"),
            requiredStringParam(params, "originalName"),
            userFromJson(user, passwordProvided),
            passwordProvided
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (method == "user.deleteUser") {
        const auto result = users_.deleteUser(
            requiredStringParam(params, "sessionId"),
            requiredStringParam(params, "name")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (method == "user.resetFailedCount") {
        const auto result = users_.resetFailedCount(
            requiredStringParam(params, "sessionId"),
            requiredStringParam(params, "name")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (method == "user.disableUser") {
        const auto result = users_.disableUser(
            requiredStringParam(params, "sessionId"),
            requiredStringParam(params, "name")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (method == "user.updateOwnAccount") {
        const bool passwordProvided = optionalBoolParam(params, "passwordProvided");
        const bool passwordHintProvided = optionalBoolParam(params, "passwordHintProvided");
        const auto result = users_.updateOwnAccount(
            requiredStringParam(params, "sessionId"),
            passwordProvided,
            passwordProvided ? requiredStringParam(params, "password") : std::string{},
            passwordHintProvided,
            passwordHintProvided ? requiredStringParam(params, "passwordHint") : std::string{}
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (method == "user.getStrictMode") {
        const auto result = users_.getStrictMode(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"enabled", protocol::JsonValue::boolean(result.value)}});
    }

    if (method == "user.setStrictMode") {
        const auto result = users_.setStrictMode(
            requiredStringParam(params, "sessionId"),
            requiredBoolParam(params, "enabled")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.listDirectory") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->listDirectory(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }

        return entriesToJson(result.value);
    }

    if (files_ != nullptr && method == "file.readFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->readFile(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"content", protocol::JsonValue::string(result.value.content)}});
    }

    if (files_ != nullptr && method == "file.writeFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const std::string content = requiredStringParam(params, "content");
        const auto result = files_->writeFile(sessionId, path, content);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.createDirectory") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->createDirectory(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.deleteFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->deleteFile(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.renameFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string from = requiredStringParam(params, "from");
        const std::string to = requiredStringParam(params, "to");
        const bool overwrite = optionalBoolParam(params, "overwrite");
        const auto result = files_->renameFile(sessionId, from, to, overwrite);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.copyFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string from = requiredStringParam(params, "from");
        const std::string to = requiredStringParam(params, "to");
        const bool overwrite = optionalBoolParam(params, "overwrite");
        const auto result = files_->copyFile(sessionId, from, to, overwrite);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.moveFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string from = requiredStringParam(params, "from");
        const std::string to = requiredStringParam(params, "to");
        const bool overwrite = optionalBoolParam(params, "overwrite");
        const auto result = files_->moveFile(sessionId, from, to, overwrite);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.removeDirectory") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const bool recursive = optionalBoolParam(params, "recursive");
        const auto result = files_->removeDirectory(sessionId, path, recursive);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.search") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string root = requiredStringParam(params, "root");
        const std::string query = requiredStringParam(params, "query");
        const auto result = files_->search(sessionId, root, query);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return entriesToJson(result.value);
    }

    if (tux_ != nullptr && method == "tux.list") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = tux_->list(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return entriesToJson(result.value);
    }

    if (tux_ != nullptr && method == "tux.create") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const bool overwrite = optionalBoolParam(params, "overwrite");
        const auto result = tux_->create(sessionId, path, overwrite);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.write") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const std::string content = requiredStringParam(params, "content");
        const auto result = tux_->write(sessionId, path, content);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.read") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = tux_->read(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({
            {"content", protocol::JsonValue::string(result.value.content)},
            {"creator", protocol::JsonValue::string(result.value.metadata.creator)},
            {"lastEditor", protocol::JsonValue::string(result.value.metadata.lastEditor)}
        });
    }

    if (tux_ != nullptr && method == "tux.delete") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = tux_->deleteFile(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.rename") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string from = requiredStringParam(params, "from");
        const std::string to = requiredStringParam(params, "to");
        const bool overwrite = optionalBoolParam(params, "overwrite");
        const auto result = tux_->renameFile(sessionId, from, to, overwrite);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.copy") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string from = requiredStringParam(params, "from");
        const std::string to = requiredStringParam(params, "to");
        const bool overwrite = optionalBoolParam(params, "overwrite");
        const auto result = tux_->copyFile(sessionId, from, to, overwrite);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.move") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string from = requiredStringParam(params, "from");
        const std::string to = requiredStringParam(params, "to");
        const bool overwrite = optionalBoolParam(params, "overwrite");
        const auto result = tux_->moveFile(sessionId, from, to, overwrite);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return protocol::JsonValue::object({{"ok", protocol::JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.search") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string root = requiredStringParam(params, "root");
        const std::string query = requiredStringParam(params, "query");
        const auto result = tux_->search(sessionId, root, query);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return entriesToJson(result.value);
    }

    throw RpcError(ErrorCode::UnknownMethod, "Unknown method.");
}

protocol::JsonValue JsonRpcDispatcher::errorResponse(const protocol::JsonValue& id, ErrorCode code, const std::string& message) const {
    return protocol::JsonValue::object({
        {"error", protocol::JsonValue::object({
            {"code", protocol::JsonValue::string(toString(code))},
            {"message", protocol::JsonValue::string(message)}
        })},
        {"id", id}
    });
}

protocol::JsonValue JsonRpcDispatcher::successResponse(const protocol::JsonValue& id, protocol::JsonValue result) const {
    return protocol::JsonValue::object({
        {"id", id},
        {"result", std::move(result)}
    });
}

} // namespace tundraux::backend


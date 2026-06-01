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

JsonValue userToJson(const BackendUser& user) {
    return JsonValue::object({
        {"name", JsonValue::string(user.name)},
        {"type", JsonValue::string(user.type)},
        {"passwordHint", JsonValue::string(user.passwordHint)},
        {"failedCount", JsonValue::number(static_cast<double>(user.failedCount))}
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

bool optionalBoolParam(const JsonValue::Object& params, const std::string& name, bool defaultValue = false) {
    const auto found = params.find(name);
    if (found == params.end()) {
        return defaultValue;
    }
    if (found->second.type() != JsonValue::Type::Boolean) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asBoolean();
}

bool requiredBoolParam(const JsonValue::Object& params, const std::string& name) {
    const auto found = params.find(name);
    if (found == params.end() || found->second.type() != JsonValue::Type::Boolean) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asBoolean();
}

std::string optionalStringParam(const JsonValue::Object& params, const std::string& name, const std::string& defaultValue = "") {
    const auto found = params.find(name);
    if (found == params.end()) {
        return defaultValue;
    }
    if (found->second.type() != JsonValue::Type::String) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asString();
}

int optionalIntParam(const JsonValue::Object& params, const std::string& name, int defaultValue = 0) {
    const auto found = params.find(name);
    if (found == params.end()) {
        return defaultValue;
    }
    if (found->second.type() != JsonValue::Type::Number) {
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

const JsonValue::Object& requiredObjectParam(const JsonValue::Object& params, const std::string& name) {
    const auto found = params.find(name);
    if (found == params.end() || found->second.type() != JsonValue::Type::Object) {
        throw RpcError(ErrorCode::InvalidParams, "Missing or invalid parameter: " + name + ".");
    }
    return found->second.asObject();
}

BackendUser userFromJson(const JsonValue::Object& object, bool requirePassword) {
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

JsonValue entriesToJson(const std::vector<FileEntry>& value) {
    JsonValue::Array entries;
    for (const auto& entry : value) {
        entries.push_back(fileEntryToJson(entry));
    }
    return JsonValue::object({{"entries", JsonValue::array(std::move(entries))}});
}

} // namespace

JsonRpcDispatcher::JsonRpcDispatcher(SessionService& sessions, UserService& users, FileService& files, TuxService& tux)
    : sessions_(sessions), users_(users), files_(&files), tux_(&tux) {}

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

    if (method == "user.currentProfile") {
        const auto result = users_.currentProfile(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"user", userToJson(result.value)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (method == "user.deleteUser") {
        const auto result = users_.deleteUser(
            requiredStringParam(params, "sessionId"),
            requiredStringParam(params, "name")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (method == "user.resetFailedCount") {
        const auto result = users_.resetFailedCount(
            requiredStringParam(params, "sessionId"),
            requiredStringParam(params, "name")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (method == "user.disableUser") {
        const auto result = users_.disableUser(
            requiredStringParam(params, "sessionId"),
            requiredStringParam(params, "name")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (method == "user.getStrictMode") {
        const auto result = users_.getStrictMode(requiredStringParam(params, "sessionId"));
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"enabled", JsonValue::boolean(result.value)}});
    }

    if (method == "user.setStrictMode") {
        const auto result = users_.setStrictMode(
            requiredStringParam(params, "sessionId"),
            requiredBoolParam(params, "enabled")
        );
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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

    if (files_ != nullptr && method == "file.createDirectory") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->createDirectory(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.deleteFile") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = files_->deleteFile(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (files_ != nullptr && method == "file.removeDirectory") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const bool recursive = optionalBoolParam(params, "recursive");
        const auto result = files_->removeDirectory(sessionId, path, recursive);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.write") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const std::string content = requiredStringParam(params, "content");
        const auto result = tux_->write(sessionId, path, content);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
    }

    if (tux_ != nullptr && method == "tux.read") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = tux_->read(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({
            {"content", JsonValue::string(result.value.content)},
            {"creator", JsonValue::string(result.value.metadata.creator)},
            {"lastEditor", JsonValue::string(result.value.metadata.lastEditor)}
        });
    }

    if (tux_ != nullptr && method == "tux.delete") {
        const std::string sessionId = requiredStringParam(params, "sessionId");
        const std::string path = requiredStringParam(params, "path");
        const auto result = tux_->deleteFile(sessionId, path);
        if (!result.ok) {
            throwIfFailed(result.error);
        }
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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
        return JsonValue::object({{"ok", JsonValue::boolean(true)}});
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

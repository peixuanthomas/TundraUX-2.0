#include "backend_client.hpp"

#include "json.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace tundraux::frontend {
namespace {

using tundraux::backend::JsonValue;

constexpr const char* transportErrorCode = "TransportError";
constexpr const char* transportErrorMessage = "Backend unavailable.";
constexpr const char* invalidResponseCode = "InvalidResponse";
constexpr const char* invalidResponseMessage = "Invalid backend response.";
constexpr double maxSafeJsonInteger = 9007199254740991.0;

template <typename T>
ClientResult<T> errorResult(std::string code, std::string message) {
    ClientResult<T> result;
    result.ok = false;
    result.errorCode = std::move(code);
    result.message = std::move(message);
    return result;
}

template <typename T>
ClientResult<T> successResult(T value) {
    ClientResult<T> result;
    result.ok = true;
    result.value = std::move(value);
    return result;
}

const JsonValue* objectField(const JsonValue::Object& object, const std::string& name) {
    const auto found = object.find(name);
    if (found == object.end()) {
        return nullptr;
    }
    return &found->second;
}

const JsonValue& requiredObjectField(const JsonValue::Object& object, const std::string& name) {
    const auto* field = objectField(object, name);
    if (field == nullptr) {
        throw std::logic_error("missing field");
    }
    return *field;
}

std::string requiredStringField(const JsonValue::Object& object, const std::string& name) {
    const auto& field = requiredObjectField(object, name);
    if (field.type() != JsonValue::Type::String) {
        throw std::logic_error("expected string");
    }
    return field.asString();
}

bool requiredBooleanField(const JsonValue::Object& object, const std::string& name) {
    const auto& field = requiredObjectField(object, name);
    if (field.type() != JsonValue::Type::Boolean) {
        throw std::logic_error("expected boolean");
    }
    return field.asBoolean();
}

FrontendUser parseUser(const JsonValue& value) {
    if (value.type() != JsonValue::Type::Object) {
        throw std::logic_error("expected user object");
    }
    const auto& object = value.asObject();
    return FrontendUser{
        requiredStringField(object, "name"),
        requiredStringField(object, "type")
    };
}

FrontendSession parseSession(const JsonValue& value) {
    if (value.type() != JsonValue::Type::Object) {
        throw std::logic_error("expected session object");
    }
    const auto& object = value.asObject();
    return FrontendSession{
        requiredStringField(object, "sessionId"),
        parseUser(requiredObjectField(object, "user"))
    };
}

FrontendFileEntry parseFileEntry(const JsonValue& value) {
    if (value.type() != JsonValue::Type::Object) {
        throw std::logic_error("expected file entry object");
    }
    const auto& object = value.asObject();
    const auto& sizeValue = requiredObjectField(object, "size");
    if (sizeValue.type() != JsonValue::Type::Number ||
        !std::isfinite(sizeValue.asNumber()) ||
        sizeValue.asNumber() < 0.0 ||
        std::floor(sizeValue.asNumber()) != sizeValue.asNumber() ||
        sizeValue.asNumber() > maxSafeJsonInteger) {
        throw std::logic_error("expected size number");
    }

    return FrontendFileEntry{
        requiredStringField(object, "name"),
        requiredStringField(object, "path"),
        requiredStringField(object, "type"),
        static_cast<unsigned long long>(sizeValue.asNumber())
    };
}

std::vector<FrontendFileEntry> parseEntriesResult(const JsonValue& result) {
    if (result.type() != JsonValue::Type::Object) {
        throw std::logic_error("expected entries result object");
    }
    const auto& entries = requiredObjectField(result.asObject(), "entries");
    if (entries.type() != JsonValue::Type::Array) {
        throw std::logic_error("expected entries array");
    }
    std::vector<FrontendFileEntry> parsedEntries;
    for (const auto& entry : entries.asArray()) {
        parsedEntries.push_back(parseFileEntry(entry));
    }
    return parsedEntries;
}

bool parseOkResult(const JsonValue& result) {
    if (result.type() != JsonValue::Type::Object) {
        throw std::logic_error("expected ok result object");
    }
    return requiredBooleanField(result.asObject(), "ok");
}

FrontendTuxContent parseTuxContent(const JsonValue& result) {
    if (result.type() != JsonValue::Type::Object) {
        throw std::logic_error("expected tux content object");
    }
    const auto& object = result.asObject();
    return FrontendTuxContent{
        requiredStringField(object, "content"),
        requiredStringField(object, "creator"),
        requiredStringField(object, "lastEditor")
    };
}

JsonValue::Object paramsWithSession(const std::string& sessionId) {
    return JsonValue::Object{{"sessionId", JsonValue::string(sessionId)}};
}

JsonValue::Object paramsWithPath(const std::string& sessionId, const std::string& path) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("path", JsonValue::string(path));
    return params;
}

JsonValue::Object paramsWithFromTo(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("from", JsonValue::string(from));
    params.emplace("to", JsonValue::string(to));
    params.emplace("overwrite", JsonValue::boolean(overwrite));
    return params;
}

JsonValue::Object paramsWithRootQuery(
    const std::string& sessionId,
    const std::string& root,
    const std::string& query
) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("root", JsonValue::string(root));
    params.emplace("query", JsonValue::string(query));
    return params;
}

JsonValue makeRequest(const std::string& id, const std::string& method, JsonValue::Object params) {
    return JsonValue::object({
        {"id", JsonValue::string(id)},
        {"method", JsonValue::string(method)},
        {"params", JsonValue::object(std::move(params))}
    });
}

template <typename T, typename Parser>
ClientResult<T> parseClientResponse(const std::string& response, const std::string& expectedId, Parser parser) {
    const auto parsed = tundraux::backend::parseJson(response);
    if (!parsed.ok || parsed.value.type() != JsonValue::Type::Object) {
        return errorResult<T>(invalidResponseCode, invalidResponseMessage);
    }

    const auto& object = parsed.value.asObject();
    const auto* id = objectField(object, "id");
    if (id == nullptr || id->type() != JsonValue::Type::String || id->asString() != expectedId) {
        return errorResult<T>(invalidResponseCode, invalidResponseMessage);
    }

    const auto* error = objectField(object, "error");
    if (error != nullptr) {
        if (error->type() != JsonValue::Type::Object) {
            return errorResult<T>(invalidResponseCode, invalidResponseMessage);
        }
        const auto& errorObject = error->asObject();
        try {
            return errorResult<T>(
                requiredStringField(errorObject, "code"),
                requiredStringField(errorObject, "message")
            );
        } catch (const std::logic_error&) {
            return errorResult<T>(invalidResponseCode, invalidResponseMessage);
        }
    }

    const auto* result = objectField(object, "result");
    if (result == nullptr) {
        return errorResult<T>(invalidResponseCode, invalidResponseMessage);
    }

    try {
        return successResult<T>(parser(*result));
    } catch (const std::logic_error&) {
        return errorResult<T>(invalidResponseCode, invalidResponseMessage);
    }
}

template <typename T, typename Parser>
ClientResult<T> sendRequest(
    BackendLineTransport& transport,
    const std::string& id,
    const std::string& method,
    JsonValue::Object params,
    Parser parser
) {
    const std::string requestLine = tundraux::backend::stringifyJson(makeRequest(id, method, std::move(params)));
    std::string response;
    if (!transport.requestLine(requestLine, response)) {
        return errorResult<T>(transportErrorCode, transportErrorMessage);
    }
    return parseClientResponse<T>(response, id, parser);
}

} // namespace

BackendClient::BackendClient(BackendLineTransport& transport) : transport_(transport) {}

std::string BackendClient::nextRequestId() {
    return std::to_string(nextId_++);
}

ClientResult<FrontendSession> BackendClient::startGuestSession() {
    return sendRequest<FrontendSession>(
        transport_,
        nextRequestId(),
        "session.startGuestSession",
        {},
        [](const JsonValue& result) { return parseSession(result); }
    );
}

ClientResult<FrontendSession> BackendClient::startSession(const FrontendUser& user) {
    JsonValue::Object userParams{
        {"name", JsonValue::string(user.name)},
        {"type", JsonValue::string(user.type)}
    };
    JsonValue::Object params{
        {"user", JsonValue::object(std::move(userParams))}
    };
    return sendRequest<FrontendSession>(
        transport_,
        nextRequestId(),
        "session.startSession",
        std::move(params),
        [](const JsonValue& result) { return parseSession(result); }
    );
}

ClientResult<FrontendSession> BackendClient::login(
    const std::string& sessionId,
    const std::string& username,
    const std::string& password
) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("username", JsonValue::string(username));
    params.emplace("password", JsonValue::string(password));
    return sendRequest<FrontendSession>(
        transport_,
        nextRequestId(),
        "session.login",
        std::move(params),
        [](const JsonValue& result) { return parseSession(result); }
    );
}

ClientResult<bool> BackendClient::logout(const std::string& sessionId) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "session.logout",
        paramsWithSession(sessionId),
        [](const JsonValue& result) {
            if (result.type() != JsonValue::Type::Object) {
                throw std::logic_error("expected logout result object");
            }
            return requiredBooleanField(result.asObject(), "ok");
        }
    );
}

ClientResult<FrontendUser> BackendClient::whoami(const std::string& sessionId) {
    return sendRequest<FrontendUser>(
        transport_,
        nextRequestId(),
        "session.whoami",
        paramsWithSession(sessionId),
        [](const JsonValue& result) {
            if (result.type() != JsonValue::Type::Object) {
                throw std::logic_error("expected whoami result object");
            }
            return parseUser(requiredObjectField(result.asObject(), "user"));
        }
    );
}

ClientResult<std::vector<FrontendUser>> BackendClient::listUsers(const std::string& sessionId) {
    return sendRequest<std::vector<FrontendUser>>(
        transport_,
        nextRequestId(),
        "user.listUsers",
        paramsWithSession(sessionId),
        [](const JsonValue& result) {
            if (result.type() != JsonValue::Type::Object) {
                throw std::logic_error("expected list users result object");
            }
            const auto& users = requiredObjectField(result.asObject(), "users");
            if (users.type() != JsonValue::Type::Array) {
                throw std::logic_error("expected users array");
            }
            std::vector<FrontendUser> parsedUsers;
            for (const auto& user : users.asArray()) {
                parsedUsers.push_back(parseUser(user));
            }
            return parsedUsers;
        }
    );
}

ClientResult<std::vector<FrontendFileEntry>> BackendClient::listDirectory(
    const std::string& sessionId,
    const std::string& path
) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("path", JsonValue::string(path));
    return sendRequest<std::vector<FrontendFileEntry>>(
        transport_,
        nextRequestId(),
        "file.listDirectory",
        std::move(params),
        [](const JsonValue& result) { return parseEntriesResult(result); }
    );
}

ClientResult<std::string> BackendClient::readFile(const std::string& sessionId, const std::string& path) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("path", JsonValue::string(path));
    return sendRequest<std::string>(
        transport_,
        nextRequestId(),
        "file.readFile",
        std::move(params),
        [](const JsonValue& result) {
            if (result.type() != JsonValue::Type::Object) {
                throw std::logic_error("expected read file result object");
            }
            return requiredStringField(result.asObject(), "content");
        }
    );
}

ClientResult<bool> BackendClient::writeFile(
    const std::string& sessionId,
    const std::string& path,
    const std::string& content
) {
    JsonValue::Object params = paramsWithSession(sessionId);
    params.emplace("path", JsonValue::string(path));
    params.emplace("content", JsonValue::string(content));
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.writeFile",
        std::move(params),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::deleteFile(const std::string& sessionId, const std::string& path) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.deleteFile",
        paramsWithPath(sessionId, path),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::renameFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.renameFile",
        paramsWithFromTo(sessionId, from, to, overwrite),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::copyFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.copyFile",
        paramsWithFromTo(sessionId, from, to, overwrite),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::moveFile(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.moveFile",
        paramsWithFromTo(sessionId, from, to, overwrite),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::createDirectory(const std::string& sessionId, const std::string& path) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.createDirectory",
        paramsWithPath(sessionId, path),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::removeDirectory(
    const std::string& sessionId,
    const std::string& path,
    bool recursive
) {
    JsonValue::Object params = paramsWithPath(sessionId, path);
    params.emplace("recursive", JsonValue::boolean(recursive));
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "file.removeDirectory",
        std::move(params),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<std::vector<FrontendFileEntry>> BackendClient::searchFiles(
    const std::string& sessionId,
    const std::string& root,
    const std::string& query
) {
    return sendRequest<std::vector<FrontendFileEntry>>(
        transport_,
        nextRequestId(),
        "file.search",
        paramsWithRootQuery(sessionId, root, query),
        [](const JsonValue& result) { return parseEntriesResult(result); }
    );
}

ClientResult<std::vector<FrontendFileEntry>> BackendClient::listTux(
    const std::string& sessionId,
    const std::string& path
) {
    return sendRequest<std::vector<FrontendFileEntry>>(
        transport_,
        nextRequestId(),
        "tux.list",
        paramsWithPath(sessionId, path),
        [](const JsonValue& result) { return parseEntriesResult(result); }
    );
}

ClientResult<bool> BackendClient::createTux(const std::string& sessionId, const std::string& path, bool overwrite) {
    JsonValue::Object params = paramsWithPath(sessionId, path);
    params.emplace("overwrite", JsonValue::boolean(overwrite));
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "tux.create",
        std::move(params),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<FrontendTuxContent> BackendClient::readTux(const std::string& sessionId, const std::string& path) {
    return sendRequest<FrontendTuxContent>(
        transport_,
        nextRequestId(),
        "tux.read",
        paramsWithPath(sessionId, path),
        [](const JsonValue& result) { return parseTuxContent(result); }
    );
}

ClientResult<bool> BackendClient::writeTux(
    const std::string& sessionId,
    const std::string& path,
    const std::string& content
) {
    JsonValue::Object params = paramsWithPath(sessionId, path);
    params.emplace("content", JsonValue::string(content));
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "tux.write",
        std::move(params),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::deleteTux(const std::string& sessionId, const std::string& path) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "tux.delete",
        paramsWithPath(sessionId, path),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::renameTux(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "tux.rename",
        paramsWithFromTo(sessionId, from, to, overwrite),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::copyTux(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "tux.copy",
        paramsWithFromTo(sessionId, from, to, overwrite),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<bool> BackendClient::moveTux(
    const std::string& sessionId,
    const std::string& from,
    const std::string& to,
    bool overwrite
) {
    return sendRequest<bool>(
        transport_,
        nextRequestId(),
        "tux.move",
        paramsWithFromTo(sessionId, from, to, overwrite),
        [](const JsonValue& result) { return parseOkResult(result); }
    );
}

ClientResult<std::vector<FrontendFileEntry>> BackendClient::searchTux(
    const std::string& sessionId,
    const std::string& root,
    const std::string& query
) {
    return sendRequest<std::vector<FrontendFileEntry>>(
        transport_,
        nextRequestId(),
        "tux.search",
        paramsWithRootQuery(sessionId, root, query),
        [](const JsonValue& result) { return parseEntriesResult(result); }
    );
}

} // namespace tundraux::frontend

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

JsonValue::Object paramsWithSession(const std::string& sessionId) {
    return JsonValue::Object{{"sessionId", JsonValue::string(sessionId)}};
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
        [](const JsonValue& result) {
            if (result.type() != JsonValue::Type::Object) {
                throw std::logic_error("expected list directory result object");
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
        [](const JsonValue& result) {
            if (result.type() != JsonValue::Type::Object) {
                throw std::logic_error("expected write file result object");
            }
            return requiredBooleanField(result.asObject(), "ok");
        }
    );
}

} // namespace tundraux::frontend

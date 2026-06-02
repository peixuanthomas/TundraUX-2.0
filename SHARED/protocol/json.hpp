#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#ifndef TUNDRAUX_PROTOCOL_JSON_HPP_INCLUDED
#define TUNDRAUX_PROTOCOL_JSON_HPP_INCLUDED
#endif

namespace tundraux::protocol {

class JsonValue {
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };

    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    JsonValue();

    static JsonValue null();
    static JsonValue boolean(bool value);
    static JsonValue number(double value);
    static JsonValue string(std::string value);
    static JsonValue array(Array value);
    static JsonValue object(Object value);

    Type type() const;
    bool asBoolean() const;
    double asNumber() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

private:
    Type type_ = Type::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

enum class JsonParseErrorCode {
    InvalidRequest
};

struct JsonParseError {
    JsonParseErrorCode code = JsonParseErrorCode::InvalidRequest;
    std::string message;
};

struct JsonParseResult {
    bool ok = false;
    JsonValue value;
    JsonParseError error;

    static JsonParseResult success(JsonValue result) {
        JsonParseResult out;
        out.ok = true;
        out.value = std::move(result);
        return out;
    }

    static JsonParseResult failure(std::string message) {
        JsonParseResult out;
        out.ok = false;
        out.error.message = std::move(message);
        return out;
    }
};

JsonParseResult parseJson(const std::string& input);
std::string stringifyJson(const JsonValue& value);

} // namespace tundraux::protocol


#pragma once

#include "session_service.hpp"

#include <map>
#include <string>
#include <vector>

namespace tundraux::backend {

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

ServiceResult<JsonValue> parseJson(const std::string& input);
std::string stringifyJson(const JsonValue& value);

} // namespace tundraux::backend

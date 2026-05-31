#include "json.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tundraux::backend {
namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    ServiceResult<JsonValue> parse() {
        try {
            skipWhitespace();
            JsonValue value = parseValue();
            skipWhitespace();
            if (!isAtEnd()) {
                return failure("Unexpected trailing characters.");
            }
            return ServiceResult<JsonValue>::success(std::move(value));
        } catch (const std::exception& ex) {
            return failure(ex.what());
        }
    }

private:
    const std::string& input_;
    std::size_t position_ = 0;

    static ServiceResult<JsonValue> failure(const std::string& message) {
        return ServiceResult<JsonValue>::failure(ErrorCode::InvalidRequest, message);
    }

    bool isAtEnd() const {
        return position_ >= input_.size();
    }

    char peek() const {
        if (isAtEnd()) {
            return '\0';
        }
        return input_[position_];
    }

    char advance() {
        if (isAtEnd()) {
            throw std::runtime_error("Unexpected end of JSON input.");
        }
        return input_[position_++];
    }

    bool match(char expected) {
        if (peek() != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    void expect(char expected, const char* message) {
        if (!match(expected)) {
            throw std::runtime_error(message);
        }
    }

    void skipWhitespace() {
        while (!isAtEnd() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            ++position_;
        }
    }

    JsonValue parseValue() {
        skipWhitespace();
        switch (peek()) {
        case '{': return parseObject();
        case '[': return parseArray();
        case '"': return JsonValue::string(parseString());
        case 't': return parseLiteral("true", JsonValue::boolean(true));
        case 'f': return parseLiteral("false", JsonValue::boolean(false));
        case 'n': return parseLiteral("null", JsonValue::null());
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                return parseNumber();
            }
            throw std::runtime_error("Expected JSON value.");
        }
    }

    JsonValue parseLiteral(const char* literal, JsonValue value) {
        for (const char* current = literal; *current != '\0'; ++current) {
            if (advance() != *current) {
                throw std::runtime_error("Invalid JSON literal.");
            }
        }
        return value;
    }

    JsonValue parseObject() {
        expect('{', "Expected object.");
        JsonValue::Object object;
        skipWhitespace();
        if (match('}')) {
            return JsonValue::object(std::move(object));
        }

        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                throw std::runtime_error("Expected object key.");
            }
            std::string key = parseString();
            skipWhitespace();
            expect(':', "Expected ':' after object key.");
            object.emplace(std::move(key), parseValue());
            skipWhitespace();
            if (match('}')) {
                break;
            }
            expect(',', "Expected ',' between object entries.");
        }

        return JsonValue::object(std::move(object));
    }

    JsonValue parseArray() {
        expect('[', "Expected array.");
        JsonValue::Array array;
        skipWhitespace();
        if (match(']')) {
            return JsonValue::array(std::move(array));
        }

        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            if (match(']')) {
                break;
            }
            expect(',', "Expected ',' between array values.");
        }

        return JsonValue::array(std::move(array));
    }

    std::string parseString() {
        expect('"', "Expected string.");
        std::string out;
        while (!isAtEnd()) {
            const char ch = advance();
            if (ch == '"') {
                return out;
            }
            if (ch == '\\') {
                out += parseEscape();
                continue;
            }
            out += ch;
        }
        throw std::runtime_error("Unterminated string.");
    }

    char parseEscape() {
        const char escaped = advance();
        switch (escaped) {
        case '"': return '"';
        case '\\': return '\\';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        default: throw std::runtime_error("Invalid string escape.");
        }
    }

    JsonValue parseNumber() {
        const std::size_t start = position_;
        match('-');
        if (std::isdigit(static_cast<unsigned char>(peek())) == 0) {
            throw std::runtime_error("Expected digit in number.");
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            advance();
        }
        if (match('.')) {
            if (std::isdigit(static_cast<unsigned char>(peek())) == 0) {
                throw std::runtime_error("Expected digit after decimal point.");
            }
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
        }

        try {
            return JsonValue::number(std::stod(input_.substr(start, position_ - start)));
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid number.");
        }
    }
};

static std::string escapeString(const std::string& input) {
    std::string out;
    for (char ch : input) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::string stringifyNumber(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}

} // namespace

JsonValue::JsonValue() = default;

JsonValue JsonValue::null() {
    return JsonValue{};
}

JsonValue JsonValue::boolean(bool value) {
    JsonValue out;
    out.type_ = Type::Boolean;
    out.boolean_ = value;
    return out;
}

JsonValue JsonValue::number(double value) {
    JsonValue out;
    out.type_ = Type::Number;
    out.number_ = value;
    return out;
}

JsonValue JsonValue::string(std::string value) {
    JsonValue out;
    out.type_ = Type::String;
    out.string_ = std::move(value);
    return out;
}

JsonValue JsonValue::array(Array value) {
    JsonValue out;
    out.type_ = Type::Array;
    out.array_ = std::move(value);
    return out;
}

JsonValue JsonValue::object(Object value) {
    JsonValue out;
    out.type_ = Type::Object;
    out.object_ = std::move(value);
    return out;
}

JsonValue::Type JsonValue::type() const {
    return type_;
}

bool JsonValue::asBoolean() const {
    return boolean_;
}

double JsonValue::asNumber() const {
    return number_;
}

const std::string& JsonValue::asString() const {
    return string_;
}

const JsonValue::Array& JsonValue::asArray() const {
    return array_;
}

const JsonValue::Object& JsonValue::asObject() const {
    return object_;
}

ServiceResult<JsonValue> parseJson(const std::string& input) {
    return JsonParser(input).parse();
}

std::string stringifyJson(const JsonValue& value) {
    switch (value.type()) {
    case JsonValue::Type::Null:
        return "null";
    case JsonValue::Type::Boolean:
        return value.asBoolean() ? "true" : "false";
    case JsonValue::Type::Number:
        return stringifyNumber(value.asNumber());
    case JsonValue::Type::String:
        return "\"" + escapeString(value.asString()) + "\"";
    case JsonValue::Type::Array: {
        std::string out = "[";
        bool first = true;
        for (const JsonValue& item : value.asArray()) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += stringifyJson(item);
        }
        out += "]";
        return out;
    }
    case JsonValue::Type::Object: {
        std::string out = "{";
        bool first = true;
        for (const auto& entry : value.asObject()) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += "\"" + escapeString(entry.first) + "\":" + stringifyJson(entry.second);
        }
        out += "}";
        return out;
    }
    }
    return "null";
}

} // namespace tundraux::backend

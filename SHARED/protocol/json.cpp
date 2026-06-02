#include "json.hpp"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tundraux::protocol {
namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    JsonParseResult parse() {
        try {
            skipWhitespace();
            JsonValue value = parseValue();
            skipWhitespace();
            if (!isAtEnd()) {
                return failure("Unexpected trailing characters.");
            }
            return JsonParseResult::success(std::move(value));
        } catch (const std::exception& ex) {
            return failure(ex.what());
        }
    }

private:
    const std::string& input_;
    std::size_t position_ = 0;

    static JsonParseResult failure(const std::string& message) {
        return JsonParseResult::failure(
            message
        );
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
        while (!isAtEnd() && isJsonWhitespace(peek())) {
            ++position_;
        }
    }

    static bool isJsonWhitespace(char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
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
            JsonValue value = parseValue();
            const auto inserted = object.emplace(std::move(key), std::move(value));
            if (!inserted.second) {
                throw std::runtime_error("Duplicate object key.");
            }
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
            if (static_cast<unsigned char>(ch) < 0x20) {
                throw std::runtime_error("Unescaped control character in string.");
            }
            if (ch == '\\') {
                out += parseEscape();
                continue;
            }
            out += ch;
        }
        throw std::runtime_error("Unterminated string.");
    }

    std::string parseEscape() {
        const char escaped = advance();
        switch (escaped) {
        case '"': return "\"";
        case '\\': return "\\";
        case '/': return "/";
        case 'b': return "\b";
        case 'f': return "\f";
        case 'n': return "\n";
        case 'r': return "\r";
        case 't': return "\t";
        case 'u': return parseUnicodeEscape();
        default: throw std::runtime_error("Invalid string escape.");
        }
    }

    std::string parseUnicodeEscape() {
        int value = 0;
        for (int i = 0; i < 4; ++i) {
            value = (value << 4) + parseHexDigit(advance());
        }
        if (value > 0x7f) {
            throw std::runtime_error("Unsupported unicode escape.");
        }
        return std::string(1, static_cast<char>(value));
    }

    int parseHexDigit(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        throw std::runtime_error("Invalid unicode escape.");
    }

    JsonValue parseNumber() {
        const std::size_t start = position_;
        match('-');
        if (std::isdigit(static_cast<unsigned char>(peek())) == 0) {
            throw std::runtime_error("Expected digit in number.");
        }
        if (match('0')) {
            if (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                throw std::runtime_error("Leading zero in number.");
            }
        } else {
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
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
            const std::string token = input_.substr(start, position_ - start);
            std::istringstream in(token);
            in.imbue(std::locale::classic());
            double value = 0.0;
            char extra = '\0';
            if (!(in >> value) || (in >> extra)) {
                throw std::runtime_error("Invalid number.");
            }
            return JsonValue::number(value);
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
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                static const char* hex = "0123456789ABCDEF";
                const auto value = static_cast<unsigned char>(ch);
                out += "\\u00";
                out += hex[(value >> 4) & 0x0f];
                out += hex[value & 0x0f];
            } else {
                out += ch;
            }
            break;
        }
    }
    return out;
}

std::string stringifyNumber(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(15) << value;
    std::string text = out.str();
    const auto decimal = text.find('.');
    if (decimal != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        return "0";
    }
    return text;
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
    if (type_ != Type::Boolean) {
        throw std::logic_error("JsonValue is not a boolean.");
    }
    return boolean_;
}

double JsonValue::asNumber() const {
    if (type_ != Type::Number) {
        throw std::logic_error("JsonValue is not a number.");
    }
    return number_;
}

const std::string& JsonValue::asString() const {
    if (type_ != Type::String) {
        throw std::logic_error("JsonValue is not a string.");
    }
    return string_;
}

const JsonValue::Array& JsonValue::asArray() const {
    if (type_ != Type::Array) {
        throw std::logic_error("JsonValue is not an array.");
    }
    return array_;
}

const JsonValue::Object& JsonValue::asObject() const {
    if (type_ != Type::Object) {
        throw std::logic_error("JsonValue is not an object.");
    }
    return object_;
}

JsonParseResult parseJson(const std::string& input) {
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

} // namespace tundraux::protocol


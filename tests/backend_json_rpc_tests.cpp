#include "json.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool expectInvalidJson(const std::string& input, const std::string& message) {
    const auto parsed = tundraux::backend::parseJson(input);
    return expect(!parsed.ok, message)
        && expect(parsed.error.code == tundraux::backend::ErrorCode::InvalidRequest, message + " error code");
}

bool runCommaLocaleNumberTest() {
    using tundraux::backend::JsonValue;
    using tundraux::backend::parseJson;
    using tundraux::backend::stringifyJson;

    const std::locale original = std::locale();
    const std::vector<std::string> localeNames = {
        "de_DE.UTF-8",
        "de-DE",
        "deu_deu.utf8",
        "German_Germany.1252",
        "fr_FR.UTF-8",
        "fr-FR"
    };

    bool foundCommaLocale = false;
    for (const std::string& localeName : localeNames) {
        try {
            const std::locale candidate(localeName.c_str());
            if (std::use_facet<std::numpunct<char>>(candidate).decimal_point() != ',') {
                continue;
            }
            std::locale::global(candidate);
            foundCommaLocale = true;
            break;
        } catch (const std::runtime_error&) {
        }
    }

    if (!foundCommaLocale) {
        return true;
    }

    const std::string json = stringifyJson(JsonValue::number(1.5));
    const auto parsed = parseJson("1.5");
    std::locale::global(original);

    return expect(json == "1.5", "number stringify should use dot decimal under comma locale: " + json)
        && expect(parsed.ok, "number parse should accept dot decimal under comma locale")
        && expect(std::fabs(parsed.value.asNumber() - 1.5) < 0.000000000001, "comma locale parsed number mismatch");
}

int main() {
    using tundraux::backend::JsonValue;
    using tundraux::backend::parseJson;
    using tundraux::backend::stringifyJson;

    const auto parsed = parseJson(R"({"id":"1","method":"session.whoami","params":{"sessionId":"session-1"}})");
    if (!expect(parsed.ok, "json parse should pass")) return 1;
    if (!expect(parsed.value.asObject().at("id").asString() == "1", "id mismatch")) return 1;
    if (!expect(parsed.value.asObject().at("params").asObject().at("sessionId").asString() == "session-1", "session id mismatch")) return 1;

    JsonValue object = JsonValue::object({
        {"id", JsonValue::string("1")},
        {"result", JsonValue::object({{"ok", JsonValue::boolean(true)}})}
    });
    const std::string json = stringifyJson(object);
    if (!expect(json == R"({"id":"1","result":{"ok":true}})", "json stringify mismatch: " + json)) return 1;

    const auto invalid = parseJson("{");
    if (!expect(!invalid.ok, "invalid json should fail")) return 1;

    if (!expectInvalidJson("01", "leading zero number should fail")) return 1;
    if (!expectInvalidJson("00", "double zero number should fail")) return 1;
    if (!expectInvalidJson("-01", "negative leading zero number should fail")) return 1;
    if (!expectInvalidJson("00.5", "leading zero decimal should fail")) return 1;
    if (!expectInvalidJson("1.", "missing decimal digit should fail")) return 1;
    if (!expectInvalidJson("-.1", "missing integer digit should fail")) return 1;
    if (!expectInvalidJson("[1,]", "trailing array comma should fail")) return 1;
    if (!expectInvalidJson(R"({"a":1,})", "trailing object comma should fail")) return 1;
    if (!expectInvalidJson(R"({"id":"a","id":"b"})", "duplicate object key should fail")) return 1;
    if (!expectInvalidJson("1e3", "exponent notation should remain unsupported")) return 1;
    if (!expectInvalidJson("\f{}", "form feed outside string should fail")) return 1;
    if (!expectInvalidJson("[1\v]", "vertical tab outside string should fail")) return 1;
    if (!expectInvalidJson(std::string("\"line\nbreak\""), "raw newline in string should fail")) return 1;

    const auto escaped = parseJson(R"("line\n\tbreak")");
    if (!expect(escaped.ok, "escaped newline and tab should parse")) return 1;
    if (!expect(escaped.value.asString() == std::string("line\n\tbreak"), "escaped string mismatch")) return 1;
    const auto simpleEscapes = parseJson(R"("\b\f\/")");
    if (!expect(simpleEscapes.ok, "backspace form-feed slash escapes should parse")) return 1;
    if (!expect(simpleEscapes.value.asString() == std::string("\b\f/"), "simple escape string mismatch")) return 1;

    const std::string controlJson = stringifyJson(JsonValue::string(std::string("a\001b", 3)));
    if (!expect(controlJson == R"("a\u0001b")", "control character should stringify as unicode escape: " + controlJson)) return 1;
    const auto reparsedControl = parseJson(controlJson);
    if (!expect(reparsedControl.ok, "unicode escaped control character should parse")) return 1;
    if (!expect(reparsedControl.value.asString() == std::string("a\001b", 3), "unicode escaped control character mismatch")) return 1;

    const std::string smallNumberJson = stringifyJson(JsonValue::number(0.000001));
    if (!expect(smallNumberJson == "0.000001", "small number should not use exponent notation: " + smallNumberJson)) return 1;
    const auto reparsedSmallNumber = parseJson(smallNumberJson);
    if (!expect(reparsedSmallNumber.ok, "small number should parse after stringify")) return 1;
    if (!expect(std::fabs(reparsedSmallNumber.value.asNumber() - 0.000001) < 0.000000000001, "small number round-trip mismatch")) return 1;
    if (!expect(stringifyJson(JsonValue::number(std::numeric_limits<double>::infinity())) == "null", "infinity should stringify safely")) return 1;
    if (!expect(stringifyJson(JsonValue::number(std::numeric_limits<double>::quiet_NaN())) == "null", "nan should stringify safely")) return 1;
    if (!expect(runCommaLocaleNumberTest(), "comma locale number behavior failed")) return 1;

    bool wrongTypeThrew = false;
    try {
        JsonValue::boolean(true).asString();
    } catch (const std::logic_error&) {
        wrongTypeThrew = true;
    }
    if (!expect(wrongTypeThrew, "wrong-type accessor should throw")) return 1;

    return 0;
}

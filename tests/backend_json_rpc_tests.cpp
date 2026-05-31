#include "json.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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
    if (!expectInvalidJson(std::string("\"line\nbreak\""), "raw newline in string should fail")) return 1;

    const auto escaped = parseJson(R"("line\n\tbreak")");
    if (!expect(escaped.ok, "escaped newline and tab should parse")) return 1;
    if (!expect(escaped.value.asString() == std::string("line\n\tbreak"), "escaped string mismatch")) return 1;

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

    bool wrongTypeThrew = false;
    try {
        JsonValue::boolean(true).asString();
    } catch (const std::logic_error&) {
        wrongTypeThrew = true;
    }
    if (!expect(wrongTypeThrew, "wrong-type accessor should throw")) return 1;

    return 0;
}

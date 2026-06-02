#include "json.hpp"

#include <iostream>

int main() {
    using tundraux::protocol::JsonValue;
    using tundraux::protocol::parseJson;
    using tundraux::protocol::stringifyJson;

    const auto parsed = parseJson(R"({"a":1})");
    if (!parsed.ok) {
        std::cerr << "parse should succeed\n";
        return 1;
    }
    if (parsed.value.type() != JsonValue::Type::Object) {
        std::cerr << "parsed value should be object\n";
        return 1;
    }

    const std::string serialized = stringifyJson(parsed.value);
    const auto reparsed = parseJson(serialized);
    if (!reparsed.ok) {
        std::cerr << "round-trip parse should succeed\n";
        return 1;
    }
    if (reparsed.value.asObject().at("a").asNumber() != 1.0) {
        std::cerr << "round-trip value mismatch\n";
        return 1;
    }

    const auto invalid = parseJson("{");
    if (invalid.ok) {
        std::cerr << "invalid json should fail\n";
        return 1;
    }

    return 0;
}

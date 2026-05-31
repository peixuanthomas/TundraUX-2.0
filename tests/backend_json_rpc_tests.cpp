#include "json.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"
#include "user_store.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class InMemoryUserStore final : public tundraux::backend::UserStore {
public:
    std::vector<tundraux::backend::BackendUser> users{
        {"admin", "alice", "Secret1", "hint", 0},
        {"user", "bob", "Secret2", "hint", 0}
    };

    std::vector<tundraux::backend::BackendUser> listUsers() const override {
        return users;
    }

    bool updateUser(const std::string& name, const tundraux::backend::BackendUser& user) override {
        for (auto& existing : users) {
            if (existing.name == name) {
                existing = user;
                return true;
            }
        }
        return false;
    }
};

} // namespace

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

bool runDispatcherTest() {
    using tundraux::backend::JsonRpcDispatcher;
    using tundraux::backend::JsonValue;
    using tundraux::backend::parseJson;
    using tundraux::backend::SessionService;
    using tundraux::backend::UserService;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    JsonRpcDispatcher dispatcher(sessions, users);

    const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    const auto guest = parseJson(guestResponse);
    if (!expect(guest.ok, "guest response should parse: " + guestResponse)) return false;
    const auto& guestObject = guest.value.asObject();
    const auto& guestResult = guestObject.at("result").asObject();
    if (!expect(guestObject.at("id").asString() == "1", "guest response id mismatch")) return false;
    const std::string sessionId = guestResult.at("sessionId").asString();
    if (!expect(!sessionId.empty(), "guest session id should not be empty")) return false;
    if (!expect(guestResult.at("user").asObject().at("type").asString() == "guest", "guest user type mismatch")) return false;
    if (!expect(guestResult.at("user").asObject().at("name").asString().empty(), "guest user name should be empty")) return false;

    const std::string loginResponse = dispatcher.handleLine(
        R"({"id":"2","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice","password":"Secret1"}})"
    );
    const auto login = parseJson(loginResponse);
    if (!expect(login.ok, "login response should parse: " + loginResponse)) return false;
    const auto& loginResult = login.value.asObject().at("result").asObject();
    if (!expect(loginResult.at("user").asObject().at("type").asString() == "admin", "login user type mismatch")) return false;

    const std::string listResponse = dispatcher.handleLine(
        R"({"id":"3","method":"user.listUsers","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto list = parseJson(listResponse);
    if (!expect(list.ok, "list response should parse: " + listResponse)) return false;
    const auto& listUsers = list.value.asObject().at("result").asObject().at("users").asArray();
    if (!expect(listUsers.size() == 2, "list users count mismatch")) return false;
    for (const auto& user : listUsers) {
        if (!expect(user.asObject().find("password") == user.asObject().end(), "list users should not expose password field")) return false;
    }

    const std::string logoutResponse = dispatcher.handleLine(
        R"({"id":"10","method":"session.logout","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto logout = parseJson(logoutResponse);
    if (!expect(logout.ok, "logout response should parse: " + logoutResponse)) return false;
    const auto& logoutObject = logout.value.asObject();
    if (!expect(logoutObject.at("id").asString() == "10", "logout response id mismatch")) return false;
    if (!expect(logoutObject.find("error") == logoutObject.end(), "logout should not return error")) return false;
    if (!expect(logoutObject.at("result").type() == JsonValue::Type::Object, "logout result should be an object")) return false;

    const std::string whoamiAfterLogoutResponse = dispatcher.handleLine(
        R"({"id":"11","method":"session.whoami","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto whoamiAfterLogout = parseJson(whoamiAfterLogoutResponse);
    if (!expect(whoamiAfterLogout.ok, "whoami after logout response should parse: " + whoamiAfterLogoutResponse)) return false;
    const auto& whoamiAfterLogoutUser = whoamiAfterLogout.value.asObject().at("result").asObject().at("user").asObject();
    if (!expect(whoamiAfterLogout.value.asObject().at("id").asString() == "11", "whoami after logout response id mismatch")) return false;
    if (!expect(whoamiAfterLogoutUser.at("type").asString() == "guest", "whoami after logout user type mismatch")) return false;
    if (!expect(whoamiAfterLogoutUser.at("name").asString().empty(), "whoami after logout user name should be empty")) return false;

    const std::string guestListResponse = dispatcher.handleLine(
        R"({"id":"12","method":"user.listUsers","params":{"sessionId":")" + sessionId + R"("}})"
    );
    const auto guestList = parseJson(guestListResponse);
    if (!expect(guestList.ok, "guest list response should parse: " + guestListResponse)) return false;
    if (!expect(guestList.value.asObject().at("id").asString() == "12", "guest list response id mismatch")) return false;
    if (!expect(guestList.value.asObject().at("error").asObject().at("code").asString() == "PermissionDenied", "guest list users code mismatch")) return false;

    const std::string invalidLoginParamsResponse = dispatcher.handleLine(
        R"({"id":"13","method":"session.login","params":{"sessionId":")" + sessionId +
        R"(","username":"alice"}})"
    );
    const auto invalidLoginParams = parseJson(invalidLoginParamsResponse);
    if (!expect(invalidLoginParams.ok, "invalid login params response should parse: " + invalidLoginParamsResponse)) return false;
    if (!expect(invalidLoginParams.value.asObject().at("id").asString() == "13", "invalid login params response id mismatch")) return false;
    if (!expect(invalidLoginParams.value.asObject().at("error").asObject().at("code").asString() == "InvalidParams", "invalid login params code mismatch")) return false;

    const std::string unknownResponse = dispatcher.handleLine(R"({"id":"4","method":"missing.method","params":{}})");
    const auto unknown = parseJson(unknownResponse);
    if (!expect(unknown.ok, "unknown response should parse: " + unknownResponse)) return false;
    if (!expect(unknown.value.asObject().at("error").asObject().at("code").asString() == "UnknownMethod", "unknown method code mismatch")) return false;

    const std::string invalidResponse = dispatcher.handleLine("{");
    const auto invalid = parseJson(invalidResponse);
    if (!expect(invalid.ok, "invalid response should parse: " + invalidResponse)) return false;
    const auto& invalidObject = invalid.value.asObject();
    if (!expect(invalidObject.at("id").type() == JsonValue::Type::Null, "invalid request id should be null")) return false;
    if (!expect(invalidObject.at("error").asObject().at("code").asString() == "InvalidRequest", "invalid request code mismatch")) return false;

    return true;
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

    if (!expect(runDispatcherTest(), "json rpc dispatcher behavior failed")) return 1;

    return 0;
}

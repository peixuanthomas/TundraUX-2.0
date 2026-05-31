# Backend Stdio Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first backend process boundary: a transport-independent backend core plus a `tundraux_backend_stdio` JSON-RPC executable that supports guest sessions, login/logout/whoami, and user listing.

**Architecture:** Implement `tundraux_backend_core` as a small service library with explicit session state, structured errors, a `UserStore` port, and a JSON-RPC dispatcher that does not depend on stdio. Add `tundraux_backend_stdio` as the first transport adapter. Keep existing `TundraUX2` behavior unchanged in this phase.

**Tech Stack:** C++17, CMake, existing `USER/udata` and `SYSTEM/crypto`, plain executable tests registered with CTest.

---

## Scope Boundary

This plan implements Phase 1 from `docs/superpowers/specs/2026-05-31-backend-frontend-process-split-design.md`.

Included:

- `tundraux_backend_core` CMake target.
- `tundraux_backend_stdio` executable.
- In-memory backend tests for sessions, login, logout, whoami, user listing, and permission failures.
- JSON parser/stringifier sufficient for JSON-RPC requests and responses used in Phase 1.
- `DataManagerUserStore` adapter for real `user_data.dat` access.
- Protocol tests against the dispatcher and stdio executable.

Excluded:

- Frontend migration to backend client.
- File APIs, TUX APIs, audit APIs, safe file writer, and lock manager.
- HTTP transport.
- Shared daemon/multi-connection runtime.

## File Structure

- Create `BACKEND/core/backend_error.hpp`: backend error code enum and result helpers.
- Create `BACKEND/core/user_store.hpp`: backend user DTO and `UserStore` interface.
- Create `BACKEND/core/session_service.hpp`.
- Create `BACKEND/core/session_service.cpp`: multi-session guest/login/logout/whoami logic.
- Create `BACKEND/core/user_service.hpp`.
- Create `BACKEND/core/user_service.cpp`: user listing with role checks.
- Create `BACKEND/core/json.hpp`.
- Create `BACKEND/core/json.cpp`: minimal JSON value, parser, and stringifier for objects, arrays, strings, booleans, numbers, and null.
- Create `BACKEND/core/json_rpc.hpp`.
- Create `BACKEND/core/json_rpc.cpp`: request parsing, method dispatch, response formatting.
- Create `BACKEND/adapters/data_manager_user_store.hpp`.
- Create `BACKEND/adapters/data_manager_user_store.cpp`: adapter from backend `UserStore` to existing `DataManager`.
- Create `BACKEND/stdio/main.cpp`: line-delimited stdin/stdout adapter.
- Create `tests/backend_core_tests.cpp`.
- Create `tests/backend_json_rpc_tests.cpp`.
- Create `tests/backend_stdio_tests.cpp`.
- Modify `CMakeLists.txt`: add backend targets and tests.

## Task 1: Add Backend Core Types

**Files:**
- Create: `BACKEND/core/backend_error.hpp`
- Create: `BACKEND/core/user_store.hpp`

- [ ] **Step 1: Write the failing test skeleton**

Add `tests/backend_core_tests.cpp` with only the include and a simple compile assertion:

```cpp
#include "backend_error.hpp"
#include "user_store.hpp"

#include <iostream>

int main() {
    const tundraux::backend::BackendError error{
        tundraux::backend::ErrorCode::PermissionDenied,
        "Access Denied."
    };
    if (tundraux::backend::toString(error.code) != "PermissionDenied") {
        std::cerr << "unexpected error code string\n";
        return 1;
    }
    tundraux::backend::BackendUser user{"admin", "alice", "", "", 0};
    if (user.type != "admin" || user.name != "alice") {
        std::cerr << "unexpected user dto\n";
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Register the failing test target**

In `CMakeLists.txt`, add this block after the existing tests:

```cmake
add_executable(backend_core_tests
    tests/backend_core_tests.cpp
)

target_include_directories(backend_core_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_compile_features(backend_core_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME backend_core_tests COMMAND backend_core_tests)
```

- [ ] **Step 3: Run the failing test build**

Run:

```powershell
cmake --build build --target backend_core_tests
```

Expected: build fails because `backend_error.hpp` and `user_store.hpp` do not exist.

- [ ] **Step 4: Add backend error definitions**

Create `BACKEND/core/backend_error.hpp`:

```cpp
#pragma once

#include <string>

namespace tundraux::backend {

enum class ErrorCode {
    InvalidRequest,
    UnknownMethod,
    InvalidParams,
    SessionExpired,
    AuthenticationFailed,
    PermissionDenied,
    InvalidPath,
    NotFound,
    AlreadyExists,
    Conflict,
    StorageError,
    InternalError
};

struct BackendError {
    ErrorCode code;
    std::string message;
};

inline const char* toString(ErrorCode code) {
    switch (code) {
    case ErrorCode::InvalidRequest: return "InvalidRequest";
    case ErrorCode::UnknownMethod: return "UnknownMethod";
    case ErrorCode::InvalidParams: return "InvalidParams";
    case ErrorCode::SessionExpired: return "SessionExpired";
    case ErrorCode::AuthenticationFailed: return "AuthenticationFailed";
    case ErrorCode::PermissionDenied: return "PermissionDenied";
    case ErrorCode::InvalidPath: return "InvalidPath";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::Conflict: return "Conflict";
    case ErrorCode::StorageError: return "StorageError";
    case ErrorCode::InternalError: return "InternalError";
    }
    return "InternalError";
}

} // namespace tundraux::backend
```

- [ ] **Step 5: Add backend user store interface**

Create `BACKEND/core/user_store.hpp`:

```cpp
#pragma once

#include <string>
#include <vector>

namespace tundraux::backend {

struct BackendUser {
    std::string type;
    std::string name;
    std::string password;
    std::string passwordHint;
    int failedCount = 0;
};

class UserStore {
public:
    virtual ~UserStore() = default;
    virtual std::vector<BackendUser> listUsers() const = 0;
    virtual bool updateUser(const std::string& name, const BackendUser& user) = 0;
};

} // namespace tundraux::backend
```

- [ ] **Step 6: Verify and commit**

Run:

```powershell
cmake --build build --target backend_core_tests
ctest --test-dir build -R backend_core_tests --output-on-failure
```

Expected: build succeeds and `backend_core_tests` passes.

Commit:

```bash
git add CMakeLists.txt BACKEND/core/backend_error.hpp BACKEND/core/user_store.hpp tests/backend_core_tests.cpp
git commit -m "feat: add backend core types"
```

## Task 2: Add Session Service

**Files:**
- Create: `BACKEND/core/session_service.hpp`
- Create: `BACKEND/core/session_service.cpp`
- Modify: `tests/backend_core_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Extend the failing test**

Replace `tests/backend_core_tests.cpp` with:

```cpp
#include "backend_error.hpp"
#include "session_service.hpp"
#include "user_store.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

class InMemoryUserStore final : public tundraux::backend::UserStore {
public:
    std::vector<tundraux::backend::BackendUser> users{
        {"admin", "alice", "Secret1", "hint", 0},
        {"user", "bob", "Secret2", "hint", 0},
        {"user", "locked", "Secret3", "hint", 8}
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

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace tundraux::backend;

    InMemoryUserStore store;
    SessionService sessions(store);

    const auto guest = sessions.startGuestSession();
    if (!expect(!guest.sessionId.empty(), "guest session id is empty")) return 1;
    if (!expect(guest.user.type == "guest", "guest type mismatch")) return 1;
    if (!expect(guest.user.name.empty(), "guest name should be empty")) return 1;

    const auto badLogin = sessions.login(guest.sessionId, "alice", "bad");
    if (!expect(!badLogin.ok, "bad login should fail")) return 1;
    if (!expect(badLogin.error.code == ErrorCode::AuthenticationFailed, "bad login error mismatch")) return 1;
    if (!expect(store.users[0].failedCount == 1, "failed login should increment count")) return 1;

    const auto lockedLogin = sessions.login(guest.sessionId, "locked", "Secret3");
    if (!expect(!lockedLogin.ok, "locked login should fail")) return 1;
    if (!expect(lockedLogin.error.code == ErrorCode::PermissionDenied, "locked login error mismatch")) return 1;

    const auto login = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "good login should pass")) return 1;
    if (!expect(login.value.user.type == "admin", "login type mismatch")) return 1;
    if (!expect(login.value.user.name == "alice", "login name mismatch")) return 1;
    if (!expect(store.users[0].failedCount == 0, "successful login should reset count")) return 1;

    const auto whoami = sessions.whoami(guest.sessionId);
    if (!expect(whoami.ok, "whoami should pass")) return 1;
    if (!expect(whoami.value.name == "alice", "whoami name mismatch")) return 1;

    const auto logout = sessions.logout(guest.sessionId);
    if (!expect(logout.ok, "logout should pass")) return 1;
    const auto afterLogout = sessions.whoami(guest.sessionId);
    if (!expect(afterLogout.ok, "whoami after logout should pass")) return 1;
    if (!expect(afterLogout.value.type == "guest", "logout should restore guest")) return 1;

    const auto missingSession = sessions.whoami("missing");
    if (!expect(!missingSession.ok, "missing session should fail")) return 1;
    if (!expect(missingSession.error.code == ErrorCode::SessionExpired, "missing session error mismatch")) return 1;

    return 0;
}
```

- [ ] **Step 2: Add the backend core library target**

In `CMakeLists.txt`, add this before `add_executable(${PROJECT_NAME})`:

```cmake
add_library(tundraux_backend_core
    BACKEND/core/session_service.cpp
)

target_include_directories(tundraux_backend_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_compile_features(tundraux_backend_core
    PUBLIC
        cxx_std_17
)
```

Update `backend_core_tests`:

```cmake
target_link_libraries(backend_core_tests
    PRIVATE
        tundraux_backend_core
)
```

- [ ] **Step 3: Run the failing test build**

Run:

```powershell
cmake --build build --target backend_core_tests
```

Expected: build fails because `session_service.hpp` and `session_service.cpp` do not exist.

- [ ] **Step 4: Add the session service header**

Create `BACKEND/core/session_service.hpp`:

```cpp
#pragma once

#include "backend_error.hpp"
#include "user_store.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace tundraux::backend {

template <typename T>
struct ServiceResult {
    bool ok = false;
    T value{};
    BackendError error{ErrorCode::InternalError, "Internal error."};

    static ServiceResult success(T result) {
        ServiceResult out;
        out.ok = true;
        out.value = std::move(result);
        return out;
    }

    static ServiceResult failure(ErrorCode code, std::string message) {
        ServiceResult out;
        out.ok = false;
        out.error = BackendError{code, std::move(message)};
        return out;
    }
};

struct EmptyResult {};

struct SessionInfo {
    std::string sessionId;
    BackendUser user;
};

class SessionService {
public:
    explicit SessionService(UserStore& users);

    SessionInfo startGuestSession();
    ServiceResult<SessionInfo> login(
        const std::string& sessionId,
        const std::string& username,
        const std::string& password
    );
    ServiceResult<EmptyResult> logout(const std::string& sessionId);
    ServiceResult<BackendUser> whoami(const std::string& sessionId) const;

private:
    UserStore& users_;
    std::unordered_map<std::string, BackendUser> sessions_;
    std::uint64_t nextSessionId_ = 1;

    std::string nextSessionId();
    static BackendUser guestUser();
};

} // namespace tundraux::backend
```

- [ ] **Step 5: Add the session service implementation**

Create `BACKEND/core/session_service.cpp`:

```cpp
#include "session_service.hpp"

#include <algorithm>
#include <utility>

namespace tundraux::backend {

SessionService::SessionService(UserStore& users) : users_(users) {}

BackendUser SessionService::guestUser() {
    return BackendUser{"guest", "", "", "", 0};
}

std::string SessionService::nextSessionId() {
    return "session-" + std::to_string(nextSessionId_++);
}

SessionInfo SessionService::startGuestSession() {
    SessionInfo session{nextSessionId(), guestUser()};
    sessions_[session.sessionId] = session.user;
    return session;
}

ServiceResult<SessionInfo> SessionService::login(
    const std::string& sessionId,
    const std::string& username,
    const std::string& password
) {
    auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::SessionExpired, "Session expired.");
    }

    auto users = users_.listUsers();
    auto found = std::find_if(users.begin(), users.end(), [&](const BackendUser& user) {
        return user.name == username;
    });
    if (found == users.end()) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::AuthenticationFailed, "User not found.");
    }
    if (found->failedCount > 7) {
        return ServiceResult<SessionInfo>::failure(ErrorCode::PermissionDenied, "User disabled due to too many failed attempts.");
    }
    if (found->password != password) {
        BackendUser updated = *found;
        updated.failedCount += 1;
        users_.updateUser(found->name, updated);
        return ServiceResult<SessionInfo>::failure(ErrorCode::AuthenticationFailed, "Incorrect password.");
    }

    BackendUser updated = *found;
    updated.failedCount = 0;
    users_.updateUser(found->name, updated);
    session->second = updated;
    return ServiceResult<SessionInfo>::success(SessionInfo{sessionId, updated});
}

ServiceResult<EmptyResult> SessionService::logout(const std::string& sessionId) {
    auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return ServiceResult<EmptyResult>::failure(ErrorCode::SessionExpired, "Session expired.");
    }
    session->second = guestUser();
    return ServiceResult<EmptyResult>::success(EmptyResult{});
}

ServiceResult<BackendUser> SessionService::whoami(const std::string& sessionId) const {
    auto session = sessions_.find(sessionId);
    if (session == sessions_.end()) {
        return ServiceResult<BackendUser>::failure(ErrorCode::SessionExpired, "Session expired.");
    }
    return ServiceResult<BackendUser>::success(session->second);
}

} // namespace tundraux::backend
```

- [ ] **Step 6: Verify and commit**

Run:

```powershell
cmake --build build --target backend_core_tests
ctest --test-dir build -R backend_core_tests --output-on-failure
```

Expected: `backend_core_tests` passes.

Commit:

```bash
git add CMakeLists.txt BACKEND/core/session_service.hpp BACKEND/core/session_service.cpp tests/backend_core_tests.cpp
git commit -m "feat: add backend session service"
```

## Task 3: Add User Service Authorization

**Files:**
- Create: `BACKEND/core/user_service.hpp`
- Create: `BACKEND/core/user_service.cpp`
- Modify: `BACKEND/core/session_service.hpp`
- Modify: `BACKEND/core/session_service.cpp`
- Modify: `tests/backend_core_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Extend the failing test**

Append this block before `return 0;` in `tests/backend_core_tests.cpp`:

```cpp
    UserService userService(store, sessions);
    const auto guestUsers = userService.listUsers(guest.sessionId);
    if (!expect(!guestUsers.ok, "guest listUsers should fail")) return 1;
    if (!expect(guestUsers.error.code == ErrorCode::PermissionDenied, "guest listUsers error mismatch")) return 1;

    const auto adminLogin = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(adminLogin.ok, "admin relogin should pass")) return 1;
    const auto adminUsers = userService.listUsers(guest.sessionId);
    if (!expect(adminUsers.ok, "admin listUsers should pass")) return 1;
    if (!expect(adminUsers.value.size() == 3, "admin listUsers count mismatch")) return 1;
    if (!expect(adminUsers.value[0].password.empty(), "listUsers should not expose passwords")) return 1;
```

Add `#include "user_service.hpp"` at the top.

- [ ] **Step 2: Run the failing test build**

Run:

```powershell
cmake --build build --target backend_core_tests
```

Expected: build fails because `UserService` is not defined.

- [ ] **Step 3: Add a session lookup method**

Add this public method declaration in `BACKEND/core/session_service.hpp`:

```cpp
    ServiceResult<BackendUser> requireSession(const std::string& sessionId) const;
```

Add this implementation in `BACKEND/core/session_service.cpp`:

```cpp
ServiceResult<BackendUser> SessionService::requireSession(const std::string& sessionId) const {
    return whoami(sessionId);
}
```

- [ ] **Step 4: Add user service header**

Create `BACKEND/core/user_service.hpp`:

```cpp
#pragma once

#include "session_service.hpp"
#include "user_store.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class UserService {
public:
    UserService(UserStore& users, const SessionService& sessions);

    ServiceResult<std::vector<BackendUser>> listUsers(const std::string& sessionId) const;

private:
    UserStore& users_;
    const SessionService& sessions_;

    static bool canManageUsers(const BackendUser& user);
};

} // namespace tundraux::backend
```

- [ ] **Step 5: Add user service implementation**

Create `BACKEND/core/user_service.cpp`:

```cpp
#include "user_service.hpp"

namespace tundraux::backend {

UserService::UserService(UserStore& users, const SessionService& sessions)
    : users_(users), sessions_(sessions) {}

bool UserService::canManageUsers(const BackendUser& user) {
    return user.type == "admin" || user.type == "debug";
}

ServiceResult<std::vector<BackendUser>> UserService::listUsers(const std::string& sessionId) const {
    const auto session = sessions_.requireSession(sessionId);
    if (!session.ok) {
        return ServiceResult<std::vector<BackendUser>>::failure(session.error.code, session.error.message);
    }
    if (!canManageUsers(session.value)) {
        return ServiceResult<std::vector<BackendUser>>::failure(ErrorCode::PermissionDenied, "Access Denied.");
    }

    auto users = users_.listUsers();
    for (auto& user : users) {
        user.password.clear();
    }
    return ServiceResult<std::vector<BackendUser>>::success(users);
}

} // namespace tundraux::backend
```

- [ ] **Step 6: Update CMake**

Add `BACKEND/core/user_service.cpp` to `tundraux_backend_core`:

```cmake
add_library(tundraux_backend_core
    BACKEND/core/session_service.cpp
    BACKEND/core/user_service.cpp
)
```

- [ ] **Step 7: Verify and commit**

Run:

```powershell
cmake --build build --target backend_core_tests
ctest --test-dir build -R backend_core_tests --output-on-failure
```

Expected: `backend_core_tests` passes.

Commit:

```bash
git add CMakeLists.txt BACKEND/core/session_service.hpp BACKEND/core/session_service.cpp BACKEND/core/user_service.hpp BACKEND/core/user_service.cpp tests/backend_core_tests.cpp
git commit -m "feat: add backend user listing service"
```

## Task 4: Add Minimal JSON Support

**Files:**
- Create: `BACKEND/core/json.hpp`
- Create: `BACKEND/core/json.cpp`
- Create: `tests/backend_json_rpc_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing JSON tests**

Create `tests/backend_json_rpc_tests.cpp`:

```cpp
#include "json.hpp"

#include <iostream>
#include <string>

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
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
    return 0;
}
```

- [ ] **Step 2: Register failing test**

Add to `CMakeLists.txt`:

```cmake
add_executable(backend_json_rpc_tests
    tests/backend_json_rpc_tests.cpp
)

target_link_libraries(backend_json_rpc_tests
    PRIVATE
        tundraux_backend_core
)

target_compile_features(backend_json_rpc_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME backend_json_rpc_tests COMMAND backend_json_rpc_tests)
```

- [ ] **Step 3: Run the failing test build**

Run:

```powershell
cmake --build build --target backend_json_rpc_tests
```

Expected: build fails because `json.hpp` and `json.cpp` do not exist.

- [ ] **Step 4: Add JSON value API**

Create `BACKEND/core/json.hpp` with this public API:

```cpp
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
```

- [ ] **Step 5: Add JSON parser and stringifier**

Create `BACKEND/core/json.cpp`. Implement recursive descent parsing for:

- objects with string keys
- arrays
- strings with escapes for `\"`, `\\`, `\n`, `\r`, and `\t`
- booleans
- null
- integer or decimal numbers

The implementation must return `ServiceResult<JsonValue>::failure(ErrorCode::InvalidRequest, "...")` for malformed JSON and must not throw parsing exceptions to callers.

Use this exact string escaping behavior for `stringifyJson`:

```cpp
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
```

- [ ] **Step 6: Update CMake source list**

Add `BACKEND/core/json.cpp` to `tundraux_backend_core`:

```cmake
add_library(tundraux_backend_core
    BACKEND/core/json.cpp
    BACKEND/core/session_service.cpp
    BACKEND/core/user_service.cpp
)
```

- [ ] **Step 7: Verify and commit**

Run:

```powershell
cmake --build build --target backend_json_rpc_tests
ctest --test-dir build -R backend_json_rpc_tests --output-on-failure
```

Expected: `backend_json_rpc_tests` passes.

Commit:

```bash
git add CMakeLists.txt BACKEND/core/json.hpp BACKEND/core/json.cpp tests/backend_json_rpc_tests.cpp
git commit -m "feat: add backend json support"
```

## Task 5: Add JSON-RPC Dispatcher

**Files:**
- Create: `BACKEND/core/json_rpc.hpp`
- Create: `BACKEND/core/json_rpc.cpp`
- Modify: `tests/backend_json_rpc_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Extend dispatcher tests**

Replace `tests/backend_json_rpc_tests.cpp` with a test that creates `InMemoryUserStore`, `SessionService`, `UserService`, and `JsonRpcDispatcher`, then verifies:

```cpp
const std::string guestResponse = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
// parse guestResponse and assert result.sessionId is present and result.user.type == "guest"

const std::string loginResponse = dispatcher.handleLine(R"({"id":"2","method":"session.login","params":{"sessionId":"session-1","username":"alice","password":"Secret1"}})");
// assert result.user.type == "admin"

const std::string listResponse = dispatcher.handleLine(R"({"id":"3","method":"user.listUsers","params":{"sessionId":"session-1"}})");
// assert result.users is an array with two users and no password field

const std::string unknownResponse = dispatcher.handleLine(R"({"id":"4","method":"missing.method","params":{}})");
// assert error.code == "UnknownMethod"

const std::string invalidResponse = dispatcher.handleLine("{");
// assert id is null and error.code == "InvalidRequest"
```

Use the existing `parseJson` helper to inspect responses. The test must fail before `json_rpc.hpp` exists.

- [ ] **Step 2: Add dispatcher header**

Create `BACKEND/core/json_rpc.hpp`:

```cpp
#pragma once

#include "json.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <string>

namespace tundraux::backend {

class JsonRpcDispatcher {
public:
    JsonRpcDispatcher(SessionService& sessions, UserService& users);

    std::string handleLine(const std::string& line);

private:
    SessionService& sessions_;
    UserService& users_;

    JsonValue dispatch(const std::string& method, const JsonValue::Object& params);
    JsonValue errorResponse(const JsonValue& id, ErrorCode code, const std::string& message) const;
    JsonValue successResponse(const JsonValue& id, JsonValue result) const;
};

} // namespace tundraux::backend
```

- [ ] **Step 3: Add dispatcher implementation**

Create `BACKEND/core/json_rpc.cpp` implementing:

- Parse request with `parseJson`.
- Require object request.
- Read string `id` if present, otherwise use JSON null.
- Require string `method`.
- Treat absent `params` as empty object.
- Route methods:
  - `session.startGuestSession`
  - `session.login`
  - `session.logout`
  - `session.whoami`
  - `user.listUsers`
- Return `UnknownMethod` for all other methods.

Response shape must match:

```json
{"id":"1","result":{"sessionId":"session-1","user":{"type":"guest","name":""}}}
```

Error shape must match:

```json
{"id":"1","error":{"code":"PermissionDenied","message":"Access Denied."}}
```

Do not include `password` in user JSON.

- [ ] **Step 4: Update CMake**

Add `BACKEND/core/json_rpc.cpp` to `tundraux_backend_core`.

- [ ] **Step 5: Verify and commit**

Run:

```powershell
cmake --build build --target backend_json_rpc_tests
ctest --test-dir build -R backend_json_rpc_tests --output-on-failure
```

Expected: dispatcher tests pass.

Commit:

```bash
git add CMakeLists.txt BACKEND/core/json_rpc.hpp BACKEND/core/json_rpc.cpp tests/backend_json_rpc_tests.cpp
git commit -m "feat: add backend json rpc dispatcher"
```

## Task 6: Add DataManager UserStore Adapter

**Files:**
- Create: `BACKEND/adapters/data_manager_user_store.hpp`
- Create: `BACKEND/adapters/data_manager_user_store.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add adapter header**

Create `BACKEND/adapters/data_manager_user_store.hpp`:

```cpp
#pragma once

#include "user_store.hpp"
#include "udata.hpp"

#include <string>
#include <vector>

namespace tundraux::backend {

class DataManagerUserStore final : public UserStore {
public:
    explicit DataManagerUserStore(std::string filename);

    std::vector<BackendUser> listUsers() const override;
    bool updateUser(const std::string& name, const BackendUser& user) override;

private:
    std::string filename_;
};

BackendUser toBackendUser(const USER& user);
USER toLegacyUser(const BackendUser& user);

} // namespace tundraux::backend
```

- [ ] **Step 2: Add adapter implementation**

Create `BACKEND/adapters/data_manager_user_store.cpp`:

```cpp
#include "data_manager_user_store.hpp"

#include <utility>

namespace tundraux::backend {

DataManagerUserStore::DataManagerUserStore(std::string filename)
    : filename_(std::move(filename)) {}

BackendUser toBackendUser(const USER& user) {
    return BackendUser{
        user.type,
        user.name,
        user.password,
        user.password_hint,
        user.count
    };
}

USER toLegacyUser(const BackendUser& user) {
    return USER{
        user.type,
        user.name,
        user.password,
        user.passwordHint,
        user.failedCount
    };
}

std::vector<BackendUser> DataManagerUserStore::listUsers() const {
    DataManager dataManager(filename_);
    std::vector<BackendUser> out;
    for (const auto& user : dataManager.GetAllUsers()) {
        out.push_back(toBackendUser(user));
    }
    return out;
}

bool DataManagerUserStore::updateUser(const std::string& name, const BackendUser& user) {
    DataManager dataManager(filename_);
    return dataManager.UpdateUser(name, toLegacyUser(user));
}

} // namespace tundraux::backend
```

- [ ] **Step 3: Add adapter CMake target**

Add this target:

```cmake
add_library(tundraux_backend_adapters
    BACKEND/adapters/data_manager_user_store.cpp
)

target_include_directories(tundraux_backend_adapters
    PUBLIC
        ${PROJECT_SOURCE_DIR}/BACKEND/adapters
    PRIVATE
        ${PROJECT_SOURCE_DIR}/USER/udata
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
)

target_link_libraries(tundraux_backend_adapters
    PUBLIC
        tundraux_backend_core
)

target_sources(tundraux_backend_adapters
    PRIVATE
        USER/udata/udata.cpp
        SYSTEM/crypto/crypto.cpp
)

target_compile_features(tundraux_backend_adapters
    PUBLIC
        cxx_std_17
)
```

- [ ] **Step 4: Verify and commit**

Run:

```powershell
cmake --build build --target tundraux_backend_adapters
```

Expected: adapter library builds.

Commit:

```bash
git add CMakeLists.txt BACKEND/adapters/data_manager_user_store.hpp BACKEND/adapters/data_manager_user_store.cpp
git commit -m "feat: add backend user data adapter"
```

## Task 7: Add stdio Backend Executable

**Files:**
- Create: `BACKEND/stdio/main.cpp`
- Create: `tests/backend_stdio_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write stdio smoke test**

Create `tests/backend_stdio_tests.cpp`:

```cpp
#include "data_manager_user_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <iostream>

int main() {
    tundraux::backend::DataManagerUserStore store("user_data.dat");
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users);

    const std::string response = dispatcher.handleLine(R"({"id":"1","method":"session.startGuestSession","params":{}})");
    if (response.find(R"("result")") == std::string::npos ||
        response.find(R"("sessionId")") == std::string::npos) {
        std::cerr << "stdio dispatcher smoke response missing session result: " << response << "\n";
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Add stdio main**

Create `BACKEND/stdio/main.cpp`:

```cpp
#include "data_manager_user_store.hpp"
#include "json_rpc.hpp"
#include "session_service.hpp"
#include "user_service.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string userDataPath = "user_data.dat";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--user-data") {
            userDataPath = argv[i + 1];
            ++i;
        }
    }

    tundraux::backend::DataManagerUserStore store(userDataPath);
    tundraux::backend::SessionService sessions(store);
    tundraux::backend::UserService users(store, sessions);
    tundraux::backend::JsonRpcDispatcher dispatcher(sessions, users);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::cout << dispatcher.handleLine(line) << std::endl;
    }

    return 0;
}
```

- [ ] **Step 3: Add executable and test target**

Add to `CMakeLists.txt`:

```cmake
add_executable(tundraux_backend_stdio
    BACKEND/stdio/main.cpp
)

target_link_libraries(tundraux_backend_stdio
    PRIVATE
        tundraux_backend_core
        tundraux_backend_adapters
)

target_compile_features(tundraux_backend_stdio
    PRIVATE
        cxx_std_17
)

add_executable(backend_stdio_tests
    tests/backend_stdio_tests.cpp
)

target_link_libraries(backend_stdio_tests
    PRIVATE
        tundraux_backend_core
        tundraux_backend_adapters
)

target_compile_features(backend_stdio_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME backend_stdio_tests COMMAND backend_stdio_tests)
```

- [ ] **Step 4: Verify and commit**

Run:

```powershell
cmake --build build --target tundraux_backend_stdio
cmake --build build --target backend_stdio_tests
ctest --test-dir build -R backend_stdio_tests --output-on-failure
```

Expected: executable builds and smoke test passes.

Commit:

```bash
git add CMakeLists.txt BACKEND/stdio/main.cpp tests/backend_stdio_tests.cpp
git commit -m "feat: add stdio backend executable"
```

## Task 8: Add End-to-End Protocol Verification

**Files:**
- Modify: `tests/backend_json_rpc_tests.cpp`
- Modify: `tests/backend_stdio_tests.cpp`

- [ ] **Step 1: Strengthen dispatcher tests**

In `tests/backend_json_rpc_tests.cpp`, add assertions for:

- `session.logout` returns `{}` or another empty success object.
- `session.whoami` after logout returns guest.
- `user.listUsers` as guest returns `PermissionDenied`.
- invalid params for `session.login` without `password` returns `InvalidParams`.

Use concrete request strings:

```cpp
dispatcher.handleLine(R"({"id":"10","method":"session.logout","params":{"sessionId":"session-1"}})");
dispatcher.handleLine(R"({"id":"11","method":"session.whoami","params":{"sessionId":"session-1"}})");
dispatcher.handleLine(R"({"id":"12","method":"user.listUsers","params":{"sessionId":"session-1"}})");
dispatcher.handleLine(R"({"id":"13","method":"session.login","params":{"sessionId":"session-1","username":"alice"}})");
```

- [ ] **Step 2: Add executable stdin/stdout manual check command**

After building, run this from PowerShell:

```powershell
'{"id":"1","method":"session.startGuestSession","params":{}}' | .\build\tundraux_backend_stdio.exe
```

Expected output contains:

```json
{"id":"1","result":{"sessionId":"session-1","user":{"type":"guest","name":""}}}
```

- [ ] **Step 3: Run all backend tests**

Run:

```powershell
cmake --build build --target backend_core_tests
cmake --build build --target backend_json_rpc_tests
cmake --build build --target backend_stdio_tests
ctest --test-dir build -R "backend_" --output-on-failure
```

Expected: all `backend_` tests pass.

- [ ] **Step 4: Commit**

Commit:

```bash
git add tests/backend_json_rpc_tests.cpp tests/backend_stdio_tests.cpp
git commit -m "test: cover backend phase one protocol flows"
```

## Task 9: Document Phase 1 Boundary

**Files:**
- Modify: `README.md`
- Modify: `README.zh-CN.md`

- [ ] **Step 1: Add backend build note to English README**

Add a short section after the build instructions:

```markdown
### Backend Phase 1

The project also builds `tundraux_backend_stdio`, the first backend process boundary for the frontend/backend split. It exposes line-delimited JSON-RPC over stdin/stdout and currently supports session startup, login/logout/whoami, and user listing. The existing `TundraUX2` frontend is not migrated to this backend yet.
```

- [ ] **Step 2: Add backend build note to Chinese README**

Add the equivalent note after the build instructions:

```markdown
### 后端第一阶段

项目也会构建 `tundraux_backend_stdio`，这是前后端进程拆分的第一条后端边界。它通过 stdin/stdout 提供按行传输的 JSON-RPC，目前支持创建会话、登录、登出、查询当前用户和列出用户。现有 `TundraUX2` 前端在本阶段尚未迁移到该后端。
```

- [ ] **Step 3: Verify docs diff and commit**

Run:

```powershell
git diff -- README.md README.zh-CN.md
```

Expected: only the backend phase note is added.

Commit:

```bash
git add README.md README.zh-CN.md
git commit -m "docs: describe backend phase one"
```

## Final Verification

- [ ] **Step 1: Build all targets**

Run:

```powershell
cmake --build build
```

Expected: `TundraUX2`, `tundraux_backend_stdio`, and all test executables build.

- [ ] **Step 2: Run all tests**

Run:

```powershell
ctest --test-dir build --output-on-failure
```

Expected: all registered tests pass.

- [ ] **Step 3: Confirm frontend remains unchanged**

Run:

```powershell
git diff HEAD~9 -- APP CORE USER SYSTEM TundraTUI
```

Expected: no frontend behavior migration in this phase. Only backend additions, `CMakeLists.txt`, tests, and README notes should appear.

- [ ] **Step 4: Confirm backend target exists**

Run:

```powershell
Get-ChildItem build -Recurse -Filter tundraux_backend_stdio.exe | Select-Object FullName
```

Expected: one `tundraux_backend_stdio.exe` path is printed.

## Follow-Up Plans

Create separate implementation plans after Phase 1 lands:

- Phase 2: frontend shell backend client and login/user-control migration.
- Phase 3: file, Explorer, TUX File Manager, editor, and audit migration.
- Phase 4: runtime modes, auto-launch, connect mode, and process lifecycle management.

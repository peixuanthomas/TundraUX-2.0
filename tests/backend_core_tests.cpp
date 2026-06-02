#include "backend_error.hpp"
#include "session_service.hpp"
#include "user_service.hpp"
#include "user_store.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

class InMemoryUserStore final : public tundraux::backend::UserStore {
public:
    bool failUpdates = false;
    std::vector<tundraux::backend::BackendUser> users{
        {"admin", "alice", "Secret1", "hint", 0},
        {"user", "bob", "Secret2", "hint", 0},
        {"debug", "debug", "Debug1", "hint", 0},
        {"user", "locked", "Secret3", "hint", 8}
    };

    std::vector<tundraux::backend::BackendUser> listUsers() const override {
        return users;
    }

    bool addUser(const tundraux::backend::BackendUser& user) override {
        users.push_back(user);
        return true;
    }

    bool updateUser(const std::string& name, const tundraux::backend::BackendUser& user) override {
        if (failUpdates) {
            return false;
        }
        for (auto& existing : users) {
            if (existing.name == name) {
                existing = user;
                return true;
            }
        }
        return false;
    }

    bool removeUser(const std::string& name) override {
        for (auto it = users.begin(); it != users.end(); ++it) {
            if (it->name == name) {
                users.erase(it);
                return true;
            }
        }
        return false;
    }

    bool getStrictMode() const override {
        return strictMode;
    }

    bool setStrictMode(bool enabled) override {
        strictMode = enabled;
        return true;
    }

    bool strictMode = false;
};

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool management_session_is_revalidated_against_store() {
    using namespace tundraux::backend;

    {
        InMemoryUserStore store;
        SessionService sessions(store);
        UserService users(store, sessions);
        const auto guest = sessions.startGuestSession();
        const auto login = sessions.login(guest.sessionId, "alice", "Secret1");
        if (!expect(login.ok, "revalidate type-change login should pass")) return false;

        for (auto& user : store.users) {
            if (user.name == "alice") {
                user.type = "user";
            }
        }

        const auto strict = users.setStrictMode(guest.sessionId, true);
        if (!expect(!strict.ok, "downgraded admin setStrictMode should fail")) return false;
        if (!expect(strict.error.code == ErrorCode::PermissionDenied, "downgraded admin setStrictMode code mismatch")) return false;
        const auto created = users.createUser(guest.sessionId, BackendUser{"user", "eve", "Secret4", "h", 0});
        if (!expect(!created.ok, "downgraded admin createUser should fail")) return false;
        if (!expect(created.error.code == ErrorCode::PermissionDenied, "downgraded admin createUser code mismatch")) return false;
        const auto deleted = users.deleteUser(guest.sessionId, "bob");
        if (!expect(!deleted.ok, "downgraded admin deleteUser should fail")) return false;
        if (!expect(deleted.error.code == ErrorCode::PermissionDenied, "downgraded admin deleteUser code mismatch")) return false;
    }

    {
        InMemoryUserStore store;
        SessionService sessions(store);
        UserService users(store, sessions);
        const auto guest = sessions.startGuestSession();
        const auto login = sessions.login(guest.sessionId, "alice", "Secret1");
        if (!expect(login.ok, "revalidate disabled login should pass")) return false;

        for (auto& user : store.users) {
            if (user.name == "alice") {
                user.failedCount = 8;
            }
        }

        const auto strict = users.setStrictMode(guest.sessionId, true);
        if (!expect(!strict.ok, "disabled admin setStrictMode should fail")) return false;
        if (!expect(strict.error.code == ErrorCode::PermissionDenied, "disabled admin setStrictMode code mismatch")) return false;
        const auto created = users.createUser(guest.sessionId, BackendUser{"user", "eve", "Secret4", "h", 0});
        if (!expect(!created.ok, "disabled admin createUser should fail")) return false;
        if (!expect(created.error.code == ErrorCode::PermissionDenied, "disabled admin createUser code mismatch")) return false;
        const auto deleted = users.deleteUser(guest.sessionId, "bob");
        if (!expect(!deleted.ok, "disabled admin deleteUser should fail")) return false;
        if (!expect(deleted.error.code == ErrorCode::PermissionDenied, "disabled admin deleteUser code mismatch")) return false;
    }

    {
        InMemoryUserStore store;
        SessionService sessions(store);
        UserService users(store, sessions);
        const auto guest = sessions.startGuestSession();
        const auto login = sessions.login(guest.sessionId, "alice", "Secret1");
        if (!expect(login.ok, "revalidate deleted login should pass")) return false;

        if (!expect(store.removeUser("alice"), "revalidate deleted should remove alice")) return false;

        const auto strict = users.setStrictMode(guest.sessionId, true);
        if (!expect(!strict.ok, "deleted admin setStrictMode should fail")) return false;
        if (!expect(strict.error.code == ErrorCode::NotFound, "deleted admin setStrictMode code mismatch")) return false;
        const auto created = users.createUser(guest.sessionId, BackendUser{"user", "eve", "Secret4", "h", 0});
        if (!expect(!created.ok, "deleted admin createUser should fail")) return false;
        if (!expect(created.error.code == ErrorCode::NotFound, "deleted admin createUser code mismatch")) return false;
        const auto deleted = users.deleteUser(guest.sessionId, "bob");
        if (!expect(!deleted.ok, "deleted admin deleteUser should fail")) return false;
        if (!expect(deleted.error.code == ErrorCode::NotFound, "deleted admin deleteUser code mismatch")) return false;
    }

    return true;
}

bool password_hint_validation_uses_effective_password() {
    using namespace tundraux::backend;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    const auto adminGuest = sessions.startGuestSession();
    const auto adminLogin = sessions.login(adminGuest.sessionId, "alice", "Secret1");
    if (!expect(adminLogin.ok, "hint validation admin login should pass")) return false;

    const auto updated = users.updateUser(
        adminGuest.sessionId,
        "bob",
        BackendUser{"user", "bob", "", "Secret2", 0},
        false
    );
    if (!expect(!updated.ok, "updateUser should reject hint matching existing password")) return false;
    if (!expect(updated.error.code == ErrorCode::InvalidParams, "updateUser hint validation code mismatch")) return false;

    const auto bobGuest = sessions.startGuestSession();
    const auto bobLogin = sessions.login(bobGuest.sessionId, "bob", "Secret2");
    if (!expect(bobLogin.ok, "hint validation bob login should pass")) return false;

    const auto ownUpdated = users.updateOwnAccount(
        bobGuest.sessionId,
        false,
        "",
        true,
        "Secret2"
    );
    if (!expect(!ownUpdated.ok, "updateOwnAccount should reject hint matching existing password")) return false;
    if (!expect(ownUpdated.error.code == ErrorCode::InvalidParams, "updateOwnAccount hint validation code mismatch")) return false;

    return true;
}

bool current_profile_rejects_disabled_session_user() {
    using namespace tundraux::backend;

    InMemoryUserStore store;
    SessionService sessions(store);
    UserService users(store, sessions);
    const auto guest = sessions.startGuestSession();
    const auto login = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(login.ok, "disabled currentProfile login should pass")) return false;

    for (auto& user : store.users) {
        if (user.name == "alice") {
            user.failedCount = 8;
        }
    }

    const auto profile = users.currentProfile(guest.sessionId);
    if (!expect(!profile.ok, "disabled currentProfile should fail")) return false;
    if (!expect(profile.error.code == ErrorCode::PermissionDenied, "disabled currentProfile code mismatch")) return false;
    return true;
}

bool synthetic_debug_session_has_manager_access_without_stored_user() {
    using namespace tundraux::backend;

    InMemoryUserStore store;
    store.users.erase(
        std::remove_if(
            store.users.begin(),
            store.users.end(),
            [](const BackendUser& user) { return user.type == "debug"; }
        ),
        store.users.end()
    );
    SessionService sessions(store);
    UserService users(store, sessions);
    const auto debug = sessions.startSession(BackendUser{"debug", "debug", "", "", 0});

    const auto profile = users.currentProfile(debug.sessionId);
    if (!expect(profile.ok, "synthetic debug currentProfile should pass")) return false;
    if (!expect(profile.value.type == "debug", "synthetic debug currentProfile type mismatch")) return false;
    if (!expect(profile.value.name == "debug", "synthetic debug currentProfile name mismatch")) return false;

    const auto listed = users.listUsers(debug.sessionId);
    if (!expect(listed.ok, "synthetic debug listUsers should pass")) return false;
    if (!expect(listed.value.size() == store.users.size(), "synthetic debug listUsers count mismatch")) return false;

    const auto strictSet = users.setStrictMode(debug.sessionId, true);
    const auto strictGet = users.getStrictMode(debug.sessionId);
    return expect(strictSet.ok, "synthetic debug setStrictMode should pass") &&
        expect(strictGet.ok, "synthetic debug getStrictMode should pass") &&
        expect(strictGet.value, "synthetic debug strict mode value mismatch");
}

} // namespace

int main() {
    using namespace tundraux::backend;

    InMemoryUserStore store;
    SessionService sessions(store);

    const auto guest = sessions.startGuestSession();
    if (!expect(!guest.sessionId.empty(), "guest session id is empty")) return 1;
    if (!expect(guest.sessionId != "session-1", "guest session id should not be predictable")) return 1;
    if (!expect(guest.sessionId.size() >= 32, "guest session id should have high entropy shape")) return 1;
    if (!expect(guest.user.type == "guest", "guest type mismatch")) return 1;
    if (!expect(guest.user.name.empty(), "guest name should be empty")) return 1;

    const auto secondGuest = sessions.startGuestSession();
    if (!expect(secondGuest.sessionId != guest.sessionId, "guest session ids should be unique")) return 1;
    if (!expect(secondGuest.sessionId != "session-2", "second guest session id should not be predictable")) return 1;

    const auto debugStartup = sessions.startSession(BackendUser{"debug", "debug", "", "", 0});
    if (!expect(!debugStartup.sessionId.empty(), "debug startup session id is empty")) return 1;
    if (!expect(debugStartup.sessionId != guest.sessionId, "debug startup session id should be unique")) return 1;
    if (!expect(debugStartup.user.type == "debug", "debug startup type mismatch")) return 1;
    if (!expect(debugStartup.user.name == "debug", "debug startup name mismatch")) return 1;
    const auto debugStartupWhoami = sessions.whoami(debugStartup.sessionId);
    if (!expect(debugStartupWhoami.ok, "debug startup whoami should pass")) return 1;
    if (!expect(debugStartupWhoami.value.type == "debug", "debug startup whoami type mismatch")) return 1;

    const auto badLogin = sessions.login(guest.sessionId, "alice", "bad");
    if (!expect(!badLogin.ok, "bad login should fail")) return 1;
    if (!expect(badLogin.error.code == ErrorCode::AuthenticationFailed, "bad login error mismatch")) return 1;
    if (!expect(badLogin.error.message == "Incorrect password for user alice.", "bad login message mismatch")) return 1;
    if (!expect(store.users[0].failedCount == 1, "failed login should increment count")) return 1;

    const auto unknownLogin = sessions.login(guest.sessionId, "missing", "bad");
    if (!expect(!unknownLogin.ok, "unknown login should fail")) return 1;
    if (!expect(unknownLogin.error.code == badLogin.error.code, "unknown login code should match bad password")) return 1;
    if (!expect(unknownLogin.error.message == "User not found: missing.", "unknown login message mismatch")) return 1;

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

    InMemoryUserStore failedCountStore;
    failedCountStore.failUpdates = true;
    SessionService failedCountSessions(failedCountStore);
    const auto failedCountGuest = failedCountSessions.startGuestSession();
    const auto failedCountLogin = failedCountSessions.login(failedCountGuest.sessionId, "alice", "bad");
    if (!expect(!failedCountLogin.ok, "failed-count persistence failure should fail login")) return 1;
    if (!expect(failedCountLogin.error.code == ErrorCode::StorageError, "failed-count persistence error mismatch")) return 1;
    if (!expect(failedCountStore.users[0].failedCount == 0, "failed-count persistence failure should not mutate store")) return 1;

    InMemoryUserStore resetStore;
    resetStore.failUpdates = true;
    SessionService resetSessions(resetStore);
    const auto resetGuest = resetSessions.startGuestSession();
    const auto resetLogin = resetSessions.login(resetGuest.sessionId, "alice", "Secret1");
    if (!expect(!resetLogin.ok, "reset persistence failure should fail login")) return 1;
    if (!expect(resetLogin.error.code == ErrorCode::StorageError, "reset persistence error mismatch")) return 1;
    const auto resetWhoami = resetSessions.whoami(resetGuest.sessionId);
    if (!expect(resetWhoami.ok, "whoami after reset persistence failure should pass")) return 1;
    if (!expect(resetWhoami.value.type == "guest", "reset persistence failure should leave session as guest")) return 1;
    if (!expect(resetWhoami.value.name.empty(), "reset persistence failure should not set session name")) return 1;

    UserService userService(store, sessions);
    const auto debugStartupUsers = userService.listUsers(debugStartup.sessionId);
    if (!expect(debugStartupUsers.ok, "debug startup listUsers should pass")) return 1;
    if (!expect(debugStartupUsers.value.size() == 4, "debug startup listUsers count mismatch")) return 1;

    const auto missingUsers = userService.listUsers("missing");
    if (!expect(!missingUsers.ok, "missing session listUsers should fail")) return 1;
    if (!expect(missingUsers.error.code == ErrorCode::SessionExpired, "missing session listUsers error mismatch")) return 1;

    const auto guestUsers = userService.listUsers(guest.sessionId);
    if (!expect(!guestUsers.ok, "guest listUsers should fail")) return 1;
    if (!expect(guestUsers.error.code == ErrorCode::PermissionDenied, "guest listUsers error mismatch")) return 1;

    const auto userLogin = sessions.login(guest.sessionId, "bob", "Secret2");
    if (!expect(userLogin.ok, "user login should pass")) return 1;
    const auto normalUsers = userService.listUsers(guest.sessionId);
    if (!expect(!normalUsers.ok, "user listUsers should fail")) return 1;
    if (!expect(normalUsers.error.code == ErrorCode::PermissionDenied, "user listUsers error mismatch")) return 1;

    const auto adminLogin = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(adminLogin.ok, "admin relogin should pass")) return 1;
    const auto adminUsers = userService.listUsers(guest.sessionId);
    if (!expect(adminUsers.ok, "admin listUsers should pass")) return 1;
    if (!expect(adminUsers.value.size() == 4, "admin listUsers count mismatch")) return 1;
    for (const auto& user : adminUsers.value) {
        if (!expect(user.password.empty(), "admin listUsers should not expose passwords")) return 1;
    }

    const auto debugLogin = sessions.login(guest.sessionId, "debug", "Debug1");
    if (!expect(debugLogin.ok, "debug login should pass")) return 1;
    const auto debugUsers = userService.listUsers(guest.sessionId);
    if (!expect(debugUsers.ok, "debug listUsers should pass")) return 1;
    if (!expect(debugUsers.value.size() == 4, "debug listUsers count mismatch")) return 1;
    for (const auto& user : debugUsers.value) {
        if (!expect(user.password.empty(), "debug listUsers should not expose passwords")) return 1;
    }

    if (!expect(store.users[0].password == "Secret1", "alice password should remain in store")) return 1;
    if (!expect(store.users[1].password == "Secret2", "bob password should remain in store")) return 1;
    if (!expect(store.users[2].password == "Debug1", "debug password should remain in store")) return 1;
    if (!expect(management_session_is_revalidated_against_store(), "management session revalidation failed")) return 1;
    if (!expect(password_hint_validation_uses_effective_password(), "effective password hint validation failed")) return 1;
    if (!expect(current_profile_rejects_disabled_session_user(), "disabled currentProfile validation failed")) return 1;
    if (!expect(synthetic_debug_session_has_manager_access_without_stored_user(), "synthetic debug manager access failed")) return 1;

    return 0;
}

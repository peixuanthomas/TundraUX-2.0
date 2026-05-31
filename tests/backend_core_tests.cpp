#include "backend_error.hpp"
#include "session_service.hpp"
#include "user_service.hpp"
#include "user_store.hpp"

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
        {"user", "locked", "Secret3", "hint", 8}
    };

    std::vector<tundraux::backend::BackendUser> listUsers() const override {
        return users;
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
    const auto guestUsers = userService.listUsers(guest.sessionId);
    if (!expect(!guestUsers.ok, "guest listUsers should fail")) return 1;
    if (!expect(guestUsers.error.code == ErrorCode::PermissionDenied, "guest listUsers error mismatch")) return 1;

    const auto adminLogin = sessions.login(guest.sessionId, "alice", "Secret1");
    if (!expect(adminLogin.ok, "admin relogin should pass")) return 1;
    const auto adminUsers = userService.listUsers(guest.sessionId);
    if (!expect(adminUsers.ok, "admin listUsers should pass")) return 1;
    if (!expect(adminUsers.value.size() == 3, "admin listUsers count mismatch")) return 1;
    if (!expect(adminUsers.value[0].password.empty(), "listUsers should not expose passwords")) return 1;

    return 0;
}

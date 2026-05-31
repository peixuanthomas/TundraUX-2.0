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

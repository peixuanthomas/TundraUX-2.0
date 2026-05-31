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

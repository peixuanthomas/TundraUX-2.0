#pragma once

#include "backend_facade.hpp"

#include "udata.hpp"

namespace tundraux::frontend {

inline ShellUser toShellUser(const USER& user) {
    return {
        user.type,
        user.name,
        user.password_hint,
        user.count
    };
}

inline USER toLegacyUser(const ShellUser& user) {
    return {
        user.type,
        user.name,
        "",
        user.passwordHint,
        user.failedCount
    };
}

} // namespace tundraux::frontend

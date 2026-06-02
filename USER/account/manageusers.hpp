#pragma once

#include "udata.hpp"

namespace tundraux::frontend {
class BackendRuntime;
}

void manage_users(USER& currentUser, tundraux::frontend::BackendRuntime* backendRuntime);

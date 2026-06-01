#pragma once

#include "udata.hpp"

namespace tundraux::frontend {
class BackendRuntime;
}

void open_account_settings(
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr
);

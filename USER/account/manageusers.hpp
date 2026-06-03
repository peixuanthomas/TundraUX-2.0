#pragma once

#include "backend_facade.hpp"

namespace tundraux::frontend {
class BackendRuntime;
class FrontendAuditSink;
}

void manage_users(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);

#pragma once

#include "backend_facade.hpp"

namespace tundraux::frontend {
class BackendRuntime;
class FrontendAuditSink;
}

void open_account_settings(
    tundraux::frontend::ShellUser& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);

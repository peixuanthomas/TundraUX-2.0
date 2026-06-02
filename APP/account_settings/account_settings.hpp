#pragma once

#include "udata.hpp"

namespace tundraux::frontend {
class BackendRuntime;
class FrontendAuditSink;
}

void open_account_settings(
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime = nullptr,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);

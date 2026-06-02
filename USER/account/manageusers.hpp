#pragma once

#include "udata.hpp"

namespace tundraux::frontend {
class BackendRuntime;
class FrontendAuditSink;
}

void manage_users(
    USER& currentUser,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink = nullptr
);

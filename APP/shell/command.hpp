#pragma once

#include <string>
#include <vector>

namespace tundraux::frontend {
class BackendRuntime;
}

void task_main(tundraux::frontend::BackendRuntime* backendRuntime = nullptr);

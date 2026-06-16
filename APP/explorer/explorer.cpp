#include "explorer.hpp"

#include "backend_runtime.hpp"
#include "console_screen.hpp"
#include "explorer_actions.hpp"
#include "explorer_backend.hpp"
#include "explorer_directory.hpp"
#include "explorer_input.hpp"
#include "explorer_render.hpp"
#include "explorer_text.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

namespace tundraux::explorer {
namespace fs = std::filesystem;

void open(
    const std::string& username,
    const std::string& usertype,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    ConsoleScreenGuard screenGuard;

    std::unique_ptr<BackendClientExplorerBackend> backend;
    ExplorerState state;
    if (backendRuntime != nullptr && !backendRuntime->filesRoot().empty()) {
        state.rootPath = normalizedPath(fs::u8path(backendRuntime->filesRoot()));
    } else {
        state.rootPath = normalizedPath(fs::current_path());
    }

    if (backendRuntime != nullptr &&
        backendRuntime->client() != nullptr &&
        !backendRuntime->sessionId().empty()) {
        backend = std::make_unique<BackendClientExplorerBackend>(
            *backendRuntime->client(),
            backendRuntime->sessionId()
        );
        state.backend = backend.get();
    }
    state.currentPath = state.rootPath;
    state.audit = auditSink;
    state.username = username;
    state.usertype = usertype;
    refresh(state);

    bool running = true;
    while (running) {
        const tundra_tui::Size size = consoleSize();
        const std::size_t rows = std::max<int>(size.height, 18) > 8
            ? static_cast<std::size_t>(std::max<int>(size.height, 18) - 8)
            : 10;
        keepCursorVisible(state, rows);
        render(state, username, usertype);
        running = handleKey(state, readKey());
    }
}

}

void open_explorer(
    const std::string& username,
    const std::string& usertype,
    tundraux::frontend::BackendRuntime* backendRuntime,
    tundraux::frontend::FrontendAuditSink* auditSink
) {
    tundraux::explorer::open(username, usertype, backendRuntime, auditSink);
}

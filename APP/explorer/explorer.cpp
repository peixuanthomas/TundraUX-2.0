#include "explorer.hpp"

#include "console_screen.hpp"
#include "explorer_actions.hpp"
#include "explorer_directory.hpp"
#include "explorer_input.hpp"
#include "explorer_render.hpp"
#include "explorer_text.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace tundraux::explorer {
namespace fs = std::filesystem;

void open(const std::string& username, const std::string& usertype) {
    ConsoleScreenGuard screenGuard;

    ExplorerState state;
    state.rootPath = normalizedPath(fs::current_path());
    state.currentPath = state.rootPath;
    state.username = username;
    state.usertype = usertype;
    refresh(state);

    bool running = true;
    while (running) {
        const COORD size = consoleSize();
        const std::size_t rows = std::max<int>(size.Y, 18) > 8
            ? static_cast<std::size_t>(std::max<int>(size.Y, 18) - 8)
            : 10;
        keepCursorVisible(state, rows);
        render(state, username, usertype);
        running = handleKey(state, readKey());
    }
}

}

void open_explorer(const std::string& username, const std::string& usertype) {
    tundraux::explorer::open(username, usertype);
}

#include "explorer_navigation.hpp"

#include "explorer_directory.hpp"
#include "explorer_text.hpp"

namespace tundraux::explorer {

void keepCursorVisible(ExplorerState& state, std::size_t rows) {
    if (rows == 0) {
        state.scroll = 0;
        return;
    }
    if (state.cursor < state.scroll) {
        state.scroll = state.cursor;
    } else if (state.cursor >= state.scroll + rows) {
        state.scroll = state.cursor - rows + 1;
    }
}

void moveUp(ExplorerState& state) {
    if (state.cursor > 0) {
        --state.cursor;
    }
}

void moveDown(ExplorerState& state) {
    if (state.cursor + 1 < state.entries.size()) {
        ++state.cursor;
    }
}

void goParent(ExplorerState& state) {
    if (isSamePath(state.currentPath, state.rootPath) || !state.currentPath.has_parent_path()) {
        state.message = "Already at explorer root";
        return;
    }

    const fs::path parentPath = state.currentPath.parent_path();
    if (!isPathInsideRoot(parentPath, state.rootPath)) {
        state.message = "Explorer root boundary reached";
        return;
    }

    state.currentPath = parentPath;
    state.cursor = 0;
    state.scroll = 0;
    refresh(state);
}

const FileEntry* selectedEntry(const ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        return nullptr;
    }
    return &state.entries[state.cursor];
}

void selectPath(ExplorerState& state, const fs::path& path) {
    for (std::size_t index = 0; index < state.entries.size(); ++index) {
        if (isSamePath(state.entries[index].path, path)) {
            state.cursor = index;
            return;
        }
    }
}

}

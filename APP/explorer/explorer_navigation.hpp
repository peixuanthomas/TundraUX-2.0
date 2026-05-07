#pragma once

#include "explorer_types.hpp"

#include <cstddef>
#include <filesystem>

namespace tundraux::explorer {
namespace fs = std::filesystem;

void keepCursorVisible(ExplorerState& state, std::size_t rows);
void moveUp(ExplorerState& state);
void moveDown(ExplorerState& state);
void goParent(ExplorerState& state);
const FileEntry* selectedEntry(const ExplorerState& state);
void selectPath(ExplorerState& state, const fs::path& path);

}

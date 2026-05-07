#pragma once

#include "explorer_types.hpp"

#include <filesystem>

namespace tundraux::explorer {
namespace fs = std::filesystem;

fs::path uniquePasteTarget(const fs::path& requestedTarget);
void markClipboard(ExplorerState& state, ClipboardMode mode);
void pasteClipboard(ExplorerState& state);

}

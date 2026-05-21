#pragma once

#include "explorer_types.hpp"

#include <filesystem>
#include <string>

namespace tundraux::explorer {
namespace fs = std::filesystem;

using SystemClipboardWriter = bool (*)(const std::string& text, std::string& error);

fs::path uniquePasteTarget(const fs::path& requestedTarget);
void markClipboard(ExplorerState& state, ClipboardMode mode);
void pasteClipboard(ExplorerState& state);
bool copySelectedFileName(ExplorerState& state, SystemClipboardWriter writer);
void copySelectedFileNameToSystemClipboard(ExplorerState& state);

}

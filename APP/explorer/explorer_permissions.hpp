#pragma once

#include "explorer_types.hpp"

#include <string>

namespace tundraux::explorer {

std::string deletePermissionError(const ExplorerState& state, const FileEntry& entry);
std::string selectedTuxPermissionStatus(const ExplorerState& state);

}

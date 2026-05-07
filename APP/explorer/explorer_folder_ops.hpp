#pragma once

#include "explorer_types.hpp"

namespace tundraux::explorer {

void beginCreateFolder(ExplorerState& state);
void createFolderFromInput(ExplorerState& state);
void handleCreateFolderInput(ExplorerState& state, const KeyPress& key);

}

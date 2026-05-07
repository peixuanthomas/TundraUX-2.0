#pragma once

#include "explorer_types.hpp"

#include <string>
#include <vector>

namespace tundraux::explorer {

std::string searchModeLabel(SearchMode mode);
void beginSearch(ExplorerState& state);
void cancelSearch(ExplorerState& state);
void cycleSearchMode(ExplorerState& state);
void runSearch(ExplorerState& state);
void moveSearchUp(ExplorerState& state);
void moveSearchDown(ExplorerState& state);
void jumpSearchTop(ExplorerState& state);
void jumpSearchBottom(ExplorerState& state);
void openSearchResult(ExplorerState& state);
void handleSearchInput(ExplorerState& state, const KeyPress& key);
std::vector<std::string> previewSearchSelected(const ExplorerState& state);

}

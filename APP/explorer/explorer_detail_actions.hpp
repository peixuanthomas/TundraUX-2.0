#pragma once

#include "explorer_types.hpp"

#include <cstddef>

namespace tundraux::explorer {

void beginShowDetails(ExplorerState& state);
std::size_t maxDetailScroll(const ExplorerState& state);
void scrollDetailsUp(ExplorerState& state);
void scrollDetailsDown(ExplorerState& state);

}

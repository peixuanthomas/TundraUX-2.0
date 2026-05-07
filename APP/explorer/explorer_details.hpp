#pragma once

#include "explorer_types.hpp"

#include <vector>

namespace tundraux::explorer {

std::vector<DetailLine> buildDetailLines(const ExplorerState& state);

}

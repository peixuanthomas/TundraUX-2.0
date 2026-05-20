#pragma once

#include "explorer_types.hpp"

#include "TundraTUI/render_engine.hpp"

#include <cstddef>
#include <string>

namespace tundraux::explorer {

tundra_tui::Size consoleSize();
std::size_t detailVisibleRows(std::size_t height);
void render(const ExplorerState& state, const std::string& username, const std::string& usertype);

}

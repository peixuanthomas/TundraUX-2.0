#pragma once

#include "explorer_types.hpp"

#include <cstddef>
#include <string>
#include <windows.h>

namespace tundraux::explorer {

COORD consoleSize();
std::size_t detailVisibleRows(std::size_t height);
void render(const ExplorerState& state, const std::string& username, const std::string& usertype);

}

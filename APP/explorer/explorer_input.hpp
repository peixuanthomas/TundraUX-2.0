#pragma once

#include "explorer_types.hpp"

namespace tundraux::explorer {

KeyPress readKey();
bool handleDetailsKey(ExplorerState& state, const KeyPress& key);
bool handleKey(ExplorerState& state, const KeyPress& key);

}

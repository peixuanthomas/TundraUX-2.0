#pragma once

#include "explorer_actions.hpp"
#include "explorer_details.hpp"
#include "explorer_directory.hpp"
#include "explorer_input.hpp"
#include "explorer_render.hpp"
#include "explorer_text.hpp"
#include "explorer_types.hpp"

#include <string>

void open_explorer(const std::string& username, const std::string& usertype);

namespace tundraux::explorer {

void open(const std::string& username, const std::string& usertype);

}

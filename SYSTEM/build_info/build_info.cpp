#include "build_info.hpp"

#include "tundraux_build_info_generated.hpp"

namespace tundraux::build_info {

const char* timestamp() {
    return generated::kBuildTimestamp;
}

} // namespace tundraux::build_info

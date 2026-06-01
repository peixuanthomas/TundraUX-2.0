if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

string(TIMESTAMP TUNDRAUX_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S %z")

file(WRITE "${OUTPUT_FILE}" "#pragma once\n\n")
file(APPEND "${OUTPUT_FILE}" "namespace tundraux::build_info::generated {\n")
file(APPEND "${OUTPUT_FILE}" "inline constexpr const char* kBuildTimestamp = \"${TUNDRAUX_BUILD_TIMESTAMP}\";\n")
file(APPEND "${OUTPUT_FILE}" "} // namespace tundraux::build_info::generated\n")

cmake_minimum_required(VERSION 3.15)

project(TundraUX2
    VERSION 2.0.0
    LANGUAGES CXX
)

if(WIN32)
    set(TUNDRAUX_BUILD_FRONTEND_DEFAULT ON)
else()
    set(TUNDRAUX_BUILD_FRONTEND_DEFAULT OFF)
endif()

option(TUNDRAUX_BUILD_FRONTEND "Build the Windows console frontend and frontend tests." ${TUNDRAUX_BUILD_FRONTEND_DEFAULT})
option(TUNDRAUX_BUILD_TESTS "Build TundraUX core test executables." ON)

if(CMAKE_CONFIGURATION_TYPES)
    set(TUNDRAUX_BUILD_ALL_TESTS_DEFAULT ON)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(TUNDRAUX_BUILD_ALL_TESTS_DEFAULT ON)
else()
    set(TUNDRAUX_BUILD_ALL_TESTS_DEFAULT OFF)
endif()

set(TUNDRAUX_BUILD_ALL_TESTS ${TUNDRAUX_BUILD_ALL_TESTS_DEFAULT})

set(TUNDRATUI_BUILD_EXAMPLES ON CACHE BOOL "Build TundraTUI example programs.")
add_subdirectory(TundraTUI)

set(TUNDRAUX_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TUNDRAUX_BUILD_INFO_HEADER "${TUNDRAUX_GENERATED_DIR}/tundraux_build_info_generated.hpp")

add_custom_target(tundraux_generate_build_info
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_FILE=${TUNDRAUX_BUILD_INFO_HEADER}
        -P ${PROJECT_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    BYPRODUCTS ${TUNDRAUX_BUILD_INFO_HEADER}
    COMMENT "Generating TundraUX build timestamp"
    VERBATIM
)

add_library(tundraux_protocol
    SHARED/protocol/json.cpp
)

target_include_directories(tundraux_protocol
    PUBLIC
        ${PROJECT_SOURCE_DIR}/SHARED/protocol
)

target_compile_features(tundraux_protocol
    PUBLIC
        cxx_std_17
)

add_library(tundraux_backend_core
    BACKEND/core/file_service.cpp
    BACKEND/core/json_rpc.cpp
    BACKEND/core/audit_service.cpp
    BACKEND/core/session_service.cpp
    BACKEND/core/tux_service.cpp
    BACKEND/core/user_service.cpp
)

target_include_directories(tundraux_backend_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(tundraux_backend_core
    PUBLIC
        tundraux_protocol
)

target_compile_features(tundraux_backend_core
    PUBLIC
        cxx_std_17
)

add_library(tundraux_backend_adapters
    BACKEND/adapters/data_manager_user_store.cpp
    BACKEND/adapters/filesystem_file_store.cpp
    BACKEND/adapters/filesystem_tux_store.cpp
)

target_include_directories(tundraux_backend_adapters
    PUBLIC
        ${PROJECT_SOURCE_DIR}/BACKEND/adapters
    PRIVATE
        ${PROJECT_SOURCE_DIR}/USER/udata
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
)

target_link_libraries(tundraux_backend_adapters
    PUBLIC
        tundraux_backend_core
    PRIVATE
        TundraTUI::TundraTUI
)

target_sources(tundraux_backend_adapters
    PRIVATE
        USER/udata/udata.cpp
        SYSTEM/crypto/crypto.cpp
)

target_compile_features(tundraux_backend_adapters
    PUBLIC
        cxx_std_17
)

add_executable(tundraux_backend_stdio
    BACKEND/stdio/main.cpp
)

target_link_libraries(tundraux_backend_stdio
    PRIVATE
        tundraux_backend_core
        tundraux_backend_adapters
        tundraux_protocol
)

target_compile_features(tundraux_backend_stdio
    PRIVATE
        cxx_std_17
)

if(TUNDRAUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(TUNDRAUX_BUILD_LEGACY_DIRECT "Build legacy direct compatibility executable." OFF)

if(TUNDRAUX_BUILD_FRONTEND)
if(NOT WIN32)
    message(FATAL_ERROR "TUNDRAUX_BUILD_FRONTEND requires Windows-specific console APIs.")
endif()

add_executable(${PROJECT_NAME})

set(TUNDRAUX_CORE_SOURCES
    CORE/main/main.cpp
    CORE/startup/hello.cpp
)

set(TUNDRAUX_APP_COMMON_SOURCES
    APP/account_settings/account_settings.cpp
    APP/backend_client/backend_client.cpp
    APP/backend_client/backend_facade.cpp
    APP/backend_client/backend_process.cpp
    APP/backend_client/backend_runtime.cpp
    APP/editor/editor.cpp
    APP/editor/editor_win.cpp
    APP/editor/tux_editor.cpp
    APP/explorer/explorer_clipboard.cpp
    APP/explorer/explorer_backend.cpp
    APP/explorer/explorer.cpp
    APP/explorer/explorer_delete.cpp
    APP/explorer/explorer_detail_actions.cpp
    APP/explorer/explorer_details.cpp
    APP/explorer/explorer_directory.cpp
    APP/explorer/explorer_folder_ops.cpp
    APP/explorer/explorer_input.cpp
    APP/explorer/explorer_navigation.cpp
    APP/explorer/explorer_open.cpp
    APP/explorer/explorer_permissions.cpp
    APP/explorer/explorer_render.cpp
    APP/explorer/explorer_search.cpp
    APP/explorer/explorer_text.cpp
    APP/shell/command.cpp
    APP/shell/commandHandlers.cpp
    APP/shell/commandReg.cpp
    APP/shell/commandRegistry.cpp
)

set(TUNDRAUX_APP_SOURCES
    ${TUNDRAUX_APP_COMMON_SOURCES}
)

set(TUNDRAUX_USER_SOURCES
    USER/account/manageusers.cpp
)

set(TUNDRAUX_SYSTEM_SOURCES
    SYSTEM/audit/audit_log.cpp
    SYSTEM/build_info/build_info.cpp
    SYSTEM/crypto/crypto.cpp
    SYSTEM/debug/debug.cpp
)

target_sources(${PROJECT_NAME}
    PRIVATE
        ${TUNDRAUX_CORE_SOURCES}
        ${TUNDRAUX_APP_SOURCES}
        ${TUNDRAUX_USER_SOURCES}
        ${TUNDRAUX_SYSTEM_SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/CORE/startup
        ${PROJECT_SOURCE_DIR}/APP/account_settings
        ${PROJECT_SOURCE_DIR}/APP/backend_client
        ${PROJECT_SOURCE_DIR}/APP/editor
        ${PROJECT_SOURCE_DIR}/APP/explorer
        ${PROJECT_SOURCE_DIR}/APP/shell
        ${PROJECT_SOURCE_DIR}/USER/account
        ${PROJECT_SOURCE_DIR}/SYSTEM/audit
        ${PROJECT_SOURCE_DIR}/SYSTEM/build_info
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
        ${PROJECT_SOURCE_DIR}/SYSTEM/debug
        ${TUNDRAUX_GENERATED_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        TundraTUI::TundraTUI
        tundraux_protocol
)

target_compile_features(${PROJECT_NAME}
    PRIVATE
        cxx_std_17
)

add_dependencies(${PROJECT_NAME} tundraux_backend_stdio tundraux_generate_build_info)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:tundraux_backend_stdio>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

if(TUNDRAUX_BUILD_LEGACY_DIRECT)
add_executable(${PROJECT_NAME}_legacy_direct)

target_sources(${PROJECT_NAME}_legacy_direct
    PRIVATE
        ${TUNDRAUX_CORE_SOURCES}
        ${TUNDRAUX_APP_COMMON_SOURCES}
        APP/legacy_direct/legacy_direct.cpp
        ${TUNDRAUX_USER_SOURCES}
        USER/udata/udata.cpp
        ${TUNDRAUX_SYSTEM_SOURCES}
)

target_include_directories(${PROJECT_NAME}_legacy_direct
    PRIVATE
        ${PROJECT_SOURCE_DIR}/CORE/startup
        ${PROJECT_SOURCE_DIR}/APP/account_settings
        ${PROJECT_SOURCE_DIR}/APP/backend_client
        ${PROJECT_SOURCE_DIR}/APP/editor
        ${PROJECT_SOURCE_DIR}/APP/explorer
        ${PROJECT_SOURCE_DIR}/APP/legacy_direct
        ${PROJECT_SOURCE_DIR}/APP/shell
        ${PROJECT_SOURCE_DIR}/USER/account
        ${PROJECT_SOURCE_DIR}/USER/udata
        ${PROJECT_SOURCE_DIR}/SYSTEM/audit
        ${PROJECT_SOURCE_DIR}/SYSTEM/build_info
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
        ${PROJECT_SOURCE_DIR}/SYSTEM/debug
        ${PROJECT_SOURCE_DIR}/BACKEND/core
        ${TUNDRAUX_GENERATED_DIR}
)

target_link_libraries(${PROJECT_NAME}_legacy_direct
    PRIVATE
        TundraTUI::TundraTUI
        tundraux_backend_core
        tundraux_protocol
)

target_compile_features(${PROJECT_NAME}_legacy_direct
    PRIVATE
        cxx_std_17
)

target_compile_definitions(${PROJECT_NAME}_legacy_direct
    PRIVATE
        TUNDRAUX_LEGACY_DIRECT_SETUP
)

add_dependencies(${PROJECT_NAME}_legacy_direct tundraux_backend_stdio tundraux_generate_build_info)
endif()

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        TUNDRAUX_DEFAULT_USER_TYPE="$<IF:$<CONFIG:Debug>,debug,guest>"
        TUNDRAUX_DEFAULT_USER_NAME="$<IF:$<CONFIG:Debug>,debug,>"
)

endif()

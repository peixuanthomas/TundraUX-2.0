cmake_minimum_required(VERSION 3.15)

project(TundraUX2
    VERSION 2.0.0
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "TundraUX2 currently requires Windows-specific console APIs.")
endif()

option(TUNDRAUX_DEBUG_STARTUP "Start TundraUX2 in debug mode without logging in." ON)

set(TUNDRATUI_BUILD_EXAMPLES ON CACHE BOOL "Build TundraTUI example programs.")
add_subdirectory(TundraTUI)

add_library(tundraux_backend_core
    BACKEND/core/file_service.cpp
    BACKEND/core/json.cpp
    BACKEND/core/json_rpc.cpp
    BACKEND/core/session_service.cpp
    BACKEND/core/user_service.cpp
)

target_include_directories(tundraux_backend_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_compile_features(tundraux_backend_core
    PUBLIC
        cxx_std_17
)

add_library(tundraux_backend_adapters
    BACKEND/adapters/data_manager_user_store.cpp
    BACKEND/adapters/filesystem_file_store.cpp
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
)

target_compile_features(tundraux_backend_stdio
    PRIVATE
        cxx_std_17
)

add_executable(${PROJECT_NAME})

set(TUNDRAUX_CORE_SOURCES
    CORE/main/main.cpp
    CORE/startup/hello.cpp
)

set(TUNDRAUX_APP_SOURCES
    APP/account_settings/account_settings.cpp
    APP/backend_client/backend_client.cpp
    APP/backend_client/backend_process.cpp
    APP/backend_client/backend_runtime.cpp
    APP/editor/editor.cpp
    APP/editor/editor_portable.cpp
    APP/editor/editor_win.cpp
    APP/explorer/explorer_clipboard.cpp
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
    APP/file_manager/TUXfile.cpp
    APP/shell/command.cpp
    APP/shell/commandHandlers.cpp
    APP/shell/commandReg.cpp
    APP/shell/commandRegistry.cpp
)

set(TUNDRAUX_USER_SOURCES
    USER/account/manageusers.cpp
    USER/udata/udata.cpp
)

set(TUNDRAUX_SYSTEM_SOURCES
    SYSTEM/audit/audit_log.cpp
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
        ${PROJECT_SOURCE_DIR}/APP/file_manager
        ${PROJECT_SOURCE_DIR}/APP/shell
        ${PROJECT_SOURCE_DIR}/USER/account
        ${PROJECT_SOURCE_DIR}/USER/udata
        ${PROJECT_SOURCE_DIR}/SYSTEM/audit
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
        ${PROJECT_SOURCE_DIR}/SYSTEM/debug
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        TundraTUI::TundraTUI
        tundraux_backend_core
)

target_compile_features(${PROJECT_NAME}
    PRIVATE
        cxx_std_17
)

add_dependencies(${PROJECT_NAME} tundraux_backend_stdio)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:tundraux_backend_stdio>
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

enable_testing()

add_executable(audit_log_tests
    tests/audit_log_tests.cpp
    SYSTEM/audit/audit_log.cpp
    SYSTEM/crypto/crypto.cpp
    USER/udata/udata.cpp
)

target_include_directories(audit_log_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/APP/editor
        ${PROJECT_SOURCE_DIR}/SYSTEM/audit
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
        ${PROJECT_SOURCE_DIR}/USER/udata
)

target_link_libraries(audit_log_tests
    PRIVATE
        TundraTUI::TundraTUI
)

target_compile_features(audit_log_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME audit_log_tests COMMAND audit_log_tests)

add_executable(explorer_permissions_tests
    tests/explorer_permissions_tests.cpp
    APP/explorer/explorer_directory.cpp
    APP/explorer/explorer_permissions.cpp
    APP/explorer/explorer_text.cpp
)

target_include_directories(explorer_permissions_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/APP/explorer
        ${PROJECT_SOURCE_DIR}/APP/file_manager
)

target_link_libraries(explorer_permissions_tests
    PRIVATE
        TundraTUI::TundraTUI
)

target_compile_features(explorer_permissions_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME explorer_permissions_tests COMMAND explorer_permissions_tests)

add_executable(explorer_clipboard_tests
    tests/explorer_clipboard_tests.cpp
    APP/explorer/explorer_clipboard.cpp
    APP/explorer/explorer_directory.cpp
    APP/explorer/explorer_navigation.cpp
    APP/explorer/explorer_permissions.cpp
    APP/explorer/explorer_text.cpp
    SYSTEM/audit/audit_log.cpp
    SYSTEM/crypto/crypto.cpp
    USER/udata/udata.cpp
)

target_include_directories(explorer_clipboard_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/APP/editor
        ${PROJECT_SOURCE_DIR}/APP/explorer
        ${PROJECT_SOURCE_DIR}/APP/file_manager
        ${PROJECT_SOURCE_DIR}/SYSTEM/audit
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
        ${PROJECT_SOURCE_DIR}/USER/udata
)

target_link_libraries(explorer_clipboard_tests
    PRIVATE
        TundraTUI::TundraTUI
)

target_compile_features(explorer_clipboard_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME explorer_clipboard_tests COMMAND explorer_clipboard_tests)

add_executable(tundratui_footer_hint_tests
    tests/tundratui_footer_hint_tests.cpp
)

target_link_libraries(tundratui_footer_hint_tests
    PRIVATE
        TundraTUI::TundraTUI
)

target_compile_features(tundratui_footer_hint_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME tundratui_footer_hint_tests COMMAND tundratui_footer_hint_tests)

add_executable(backend_core_tests
    tests/backend_core_tests.cpp
)

target_include_directories(backend_core_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(backend_core_tests
    PRIVATE
        tundraux_backend_core
)

target_compile_features(backend_core_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME backend_core_tests COMMAND backend_core_tests)

add_executable(backend_file_service_tests
    tests/backend_file_service_tests.cpp
)

target_link_libraries(backend_file_service_tests
    PRIVATE
        tundraux_backend_adapters
)

target_compile_features(backend_file_service_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME backend_file_service_tests COMMAND backend_file_service_tests)

add_executable(backend_json_rpc_tests
    tests/backend_json_rpc_tests.cpp
)

target_link_libraries(backend_json_rpc_tests
    PRIVATE
        tundraux_backend_core
)

target_compile_features(backend_json_rpc_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME backend_json_rpc_tests COMMAND backend_json_rpc_tests)

add_executable(backend_stdio_tests
    tests/backend_stdio_tests.cpp
)

target_include_directories(backend_stdio_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
)

target_link_libraries(backend_stdio_tests
    PRIVATE
        tundraux_backend_core
        tundraux_backend_adapters
)

target_compile_features(backend_stdio_tests
    PRIVATE
        cxx_std_17
)

add_dependencies(backend_stdio_tests tundraux_backend_stdio)

add_test(NAME backend_stdio_tests COMMAND backend_stdio_tests $<TARGET_FILE:tundraux_backend_stdio>)

add_executable(frontend_backend_client_tests
    tests/frontend_backend_client_tests.cpp
    APP/backend_client/backend_client.cpp
    APP/backend_client/backend_process.cpp
    APP/backend_client/backend_runtime.cpp
)

target_include_directories(frontend_backend_client_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/APP/backend_client
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(frontend_backend_client_tests
    PRIVATE
        tundraux_backend_core
)

target_compile_features(frontend_backend_client_tests
    PRIVATE
        cxx_std_17
)

add_test(NAME frontend_backend_client_tests COMMAND frontend_backend_client_tests)

if(TUNDRAUX_DEBUG_STARTUP)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            TUNDRAUX_DEFAULT_USER_TYPE="debug"
            TUNDRAUX_DEFAULT_USER_NAME="debug"
    )
else()
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            TUNDRAUX_DEFAULT_USER_TYPE="guest"
            TUNDRAUX_DEFAULT_USER_NAME=""
    )
endif()

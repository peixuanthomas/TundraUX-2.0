cmake_minimum_required(VERSION 3.15)

project(TundraUX2
    VERSION 2.0.0
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "TundraUX2 currently requires Windows-specific console APIs.")
endif()

option(TUNDRAUX_DEBUG_STARTUP "Start TundraUX2 in debug mode without logging in." ON)

add_executable(${PROJECT_NAME})

set(TUNDRAUX_CORE_SOURCES
    CORE/main/main.cpp
    CORE/startup/hello.cpp
)

set(TUNDRAUX_APP_SOURCES
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
    SYSTEM/console/color.cpp
    SYSTEM/console/console_screen.cpp
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
        ${PROJECT_SOURCE_DIR}/APP/editor
        ${PROJECT_SOURCE_DIR}/APP/explorer
        ${PROJECT_SOURCE_DIR}/APP/file_manager
        ${PROJECT_SOURCE_DIR}/APP/shell
        ${PROJECT_SOURCE_DIR}/USER/account
        ${PROJECT_SOURCE_DIR}/USER/udata
        ${PROJECT_SOURCE_DIR}/SYSTEM/console
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
        ${PROJECT_SOURCE_DIR}/SYSTEM/debug
)

target_compile_features(${PROJECT_NAME}
    PRIVATE
        cxx_std_17
)

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

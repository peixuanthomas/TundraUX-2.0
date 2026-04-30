cmake_minimum_required(VERSION 3.15)

project(TundraUX2
    VERSION 2.0.0
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "TundraUX2 currently requires Windows-specific console APIs.")
endif()

option(TUNDRAUX_DEBUG_STARTUP "Start TundraUX2 in debug mode without logging in." OFF)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PRIVATE
        src/TUXfile.cpp
        src/color.cpp
        src/command.cpp
        src/commandHandlers.cpp
        src/commandReg.cpp
        src/commandRegistry.cpp
        src/console_screen.cpp
        src/crypto.cpp
        src/debug.cpp
        src/editor.cpp
        src/editor_portable.cpp
        src/editor_win.cpp
        src/hello.cpp
        src/main.cpp
        src/manageusers.cpp
        src/udata.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/inc
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

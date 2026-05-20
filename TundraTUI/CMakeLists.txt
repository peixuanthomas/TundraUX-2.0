cmake_minimum_required(VERSION 3.15)

project(TundraTUI
    VERSION 0.1.0
    LANGUAGES CXX
)

option(TUNDRATUI_BUILD_EXAMPLES "Build TundraTUI example programs." OFF)

add_library(TundraTUI
    src/color.cpp
    src/input.cpp
    src/render_engine.cpp
    src/screen.cpp
)

add_library(TundraTUI::TundraTUI ALIAS TundraTUI)

target_include_directories(TundraTUI
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(TundraTUI
    PUBLIC
        cxx_std_17
)

if(WIN32)
    target_compile_definitions(TundraTUI PUBLIC TUNDRATUI_PLATFORM_WINDOWS=1)
else()
    target_compile_definitions(TundraTUI PUBLIC TUNDRATUI_PLATFORM_ANSI=1)
endif()

if(TUNDRATUI_BUILD_EXAMPLES)
    add_executable(tundratui_style_demo examples/style_demo.cpp)
    target_link_libraries(tundratui_style_demo PRIVATE TundraTUI::TundraTUI)

    add_executable(tundratui_render_demo examples/render_demo.cpp)
    target_link_libraries(tundratui_render_demo PRIVATE TundraTUI::TundraTUI)
endif()

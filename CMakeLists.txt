cmake_minimum_required(VERSION 3.22)
# Deployment target and architectures must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
project(ShepardToneBuilder VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STB_BUILD_PLUGIN "Build the plug-in (needs JUCE)" ON)
set(STB_JUCE_PATH "" CACHE PATH "Local JUCE checkout; empty = download JUCE 8.0.4")

# core: no JUCE
add_library(stb_core STATIC core/Tonal.cpp core/Tempo.cpp core/Midi.cpp)
target_include_directories(stb_core PUBLIC core)
set_target_properties(stb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(stb_core PRIVATE -ffp-contract=off -Wall -Wextra)

add_executable(shepard_cli cli/shepard_main.cpp)
target_link_libraries(shepard_cli PRIVATE stb_core)
add_executable(stb_tests tests/core_test.cpp)
target_link_libraries(stb_tests PRIVATE stb_core)

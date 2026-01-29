# THIS IS A TEMPORARY ROOT CMAKELISTS.TXT WHILE FILES ARE BEING REFACTORED
cmake_minimum_required(VERSION 3.16)

project(Skewer)

# Set C++ Standard (C++17 is good for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Fixes CMake FetchContent warning (https://cmake.org/cmake/help/latest/policy/CMP0135.html)
cmake_policy(SET CMP0135 NEW)

# Fetch nlohmann/json for scene loading
include(FetchContent)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

# Tell CMake where to look for header files
# allows us to write #include "core/vec3.h" instead of "../src/core/vec3.h"
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/external)

# Define Source Files
# list every .cpp file here. no need for .h
set(SOURCES
    apps/cli/main.cc         # Renamed main file for now
    # Add new .cc files here as they're created, e.g.:
    # src/renderer/engine.cpp
    # src/renderer/film.cpp
)

# Create Executable
add_executable(render_cli ${SOURCES})
target_link_libraries(render_cli PRIVATE nlohmann_json::nlohmann_json)

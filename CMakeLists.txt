# THIS IS A TEMPORARY ROOT CMAKELISTS.TXT WHILE FILES ARE BEING REFACTORED
cmake_minimum_required(VERSION 3.16)

project(Skewer LANGUAGES CXX)

# Set C++ Standard (C++17 is good for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Fixes CMake FetchContent warning (https://cmake.org/cmake/help/latest/policy/CMP0135.html)
cmake_policy(SET CMP0135 NEW)

# Fetch nlohmann/json for scene loading
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# Define Source Files
# list every .cpp file here. no need for .h
set(SOURCES
    apps/cli/main.cc         # Renamed main file for now
    # Add new .cc files here as they're created, e.g.:
    src/session/render_session.cc
    src/film/film.cc
    src/film/image_buffer.cc
    
)

# Create Executable
add_executable(skewer-render ${SOURCES})

# Include directories tell CMake where to look for header files
target_include_directories(skewer-render
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external
)

# Link libraries
target_link_libraries(skewer-render PRIVATE nlohmann_json::nlohmann_json)

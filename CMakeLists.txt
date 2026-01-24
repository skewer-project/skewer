# THIS IS A TEMPORARY ROOT CMAKELISTS.TXT WHILE FILES ARE BEING REFACTORED
cmake_minimum_required(VERSION 3.10)

project(ray-tracer) # change name eventually

# Set C++ Standard (C++17 is good for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell CMake where to look for header files
# allows us to write #include "core/vec3.h" instead of "../src/core/vec3.h"
include_directories(${CMAKE_SOURCE_DIR}/src)

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
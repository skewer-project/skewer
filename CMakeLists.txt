# THIS IS A TEMPORARY ROOT CMAKELISTS.TXT WHILE FILES ARE BEING REFACTORED
cmake_minimum_required(VERSION 3.16)

project(Skewer LANGUAGES CXX)

# Set C++ Standard (C++17 is good for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Fixes CMake FetchContent warning
# (https://cmake.org/cmake/help/latest/policy/CMP0135.html)
cmake_policy(SET CMP0135 NEW)

# Build as STATIC libraries (easier to bundle, no .dll/.so missing errors)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Don't waste time building their tests or documentation
set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(IMATH_INSTALL OFF CACHE BOOL "" FORCE)
# We don't need 'exrheader' or examples

# Fetch nlohmann/json for scene loading
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

FetchContent_Declare(
  Imath
  GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/Imath.git
  GIT_TAG        v3.1.11
)

FetchContent_Declare(
  OpenEXR
  GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openexr.git
  GIT_TAG        v3.2.4
)

FetchContent_MakeAvailable(nlohmann_json Imath OpenEXR)
# Define Source Files
# list every .cpp file here. no need for .h
set(SOURCES
    apps/cli/main.cc
    src/session/render_session.cc
    src/scene/scene.cc
    src/film/film.cc
    src/film/image_buffer.cc
    src/integrators/path_trace.cc
    src/integrators/normals.cc
    src/accelerators/bvh.cc
    src/scene/light.cc
    src/io/obj_loader.cc
)

# Create Executable
add_executable(skewer-render ${SOURCES})

# Include directories tell CMake where to look for header files
target_include_directories(skewer-render
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external
)

# Apparently zlib is sometimes required (I guess for compression?)
find_package(ZLIB REQUIRED)

# Link libraries
target_link_libraries(skewer-render
    PRIVATE
    nlohmann_json::nlohmann_json
    Imath::Imath
    OpenEXR::OpenEXR
    ZLIB::ZLIB
)

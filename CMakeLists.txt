# THIS IS A TEMPORARY ROOT CMAKELISTS.TXT WHILE FILES ARE BEING REFACTORED
cmake_minimum_required(VERSION 3.16)

project(Skewer LANGUAGES CXX)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebugInfo)
endif()

# Set C++ Standard (C++17 is good for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Fixes CMake FetchContent warning
# (https://cmake.org/cmake/help/latest/policy/CMP0135.html)
cmake_policy(SET CMP0135 NEW)

# Compiler Optimizations
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-O3 -flto -ffast-math -mcpu=native)
    # Optional: -flto (Link Time Optimization) for even more speed but slower build
endif()

# Use system-installed OpenEXR and Imath (install via apt-get or brew)
find_package(Imath REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(ZLIB REQUIRED) # Apparently zlib is sometimes required

# Fetch nlohmann/json for scene loading
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json googletest)

set(BUILD_TESTING ON CACHE BOOL "" FORCE)

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
    src/io/scene_loader.cc
    src/io/image_io.cc
    src/materials/bsdf.cc
    src/core/spectral/rgb2spec.cc
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
target_link_libraries(skewer-render
    PRIVATE
    nlohmann_json::nlohmann_json
    Imath::Imath
    OpenEXR::OpenEXR
    ZLIB::ZLIB
)
target_link_options(skewer-render PRIVATE
    -flto
)

include(GoogleTest)
enable_testing()
add_subdirectory(tests)

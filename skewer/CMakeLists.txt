cmake_minimum_required(VERSION 3.16)

project(Skewer LANGUAGES CXX)

# Respect top-level build type when included as a subproject
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebugInfo)
endif()

# Set C++ Standard (C++20 is good for modern features)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
# Fixes CMake FetchContent warning
# (https://cmake.org/cmake/help/latest/policy/CMP0135.html)
cmake_policy(SET CMP0135 NEW)

option(SKEWER_BUILD_NATIVE_OPTIMIZATIONS "Enable native CPU tuning for skewer-render" ON)

# Fetch nlohmann/json for scene loading
include(FetchContent)
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(BUILD_TESTING AND NOT TARGET GTest::gtest_main)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

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
    src/materials/texture.cc
    src/core/spectral/rgb2spec.cc
)

# Create Executable
add_executable(skewer-render ${SOURCES})

# Include directories tell CMake where to look for header files
target_include_directories(skewer-render
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/external
)

set_source_files_properties(src/io/scene_loader.cc PROPERTIES
    COMPILE_OPTIONS "-fno-fast-math"
)

# Link libraries
target_link_libraries(skewer-render
    PRIVATE
    nlohmann_json::nlohmann_json
    exrio::exrio
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(skewer-render PRIVATE -ffast-math)
    if(SKEWER_BUILD_NATIVE_OPTIMIZATIONS)
        target_compile_options(skewer-render PRIVATE -mcpu=native)
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT SKEWER_IPO_SUPPORTED OUTPUT SKEWER_IPO_OUTPUT)
if(SKEWER_IPO_SUPPORTED)
    set_property(TARGET skewer-render PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set_property(TARGET skewer-render PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

if(BUILD_TESTING)
    include(GoogleTest)
    enable_testing()
    add_subdirectory(tests)
    target_compile_options(gtest PRIVATE "-fno-fast-math")
endif()

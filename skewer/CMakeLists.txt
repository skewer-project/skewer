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

option(SKEWER_BUILD_NATIVE_OPTIMIZATIONS "Enable native CPU tuning for skewer-render" OFF)

# Fetch nlohmann/json for scene loading
include(FetchContent)
if(NOT TARGET nlohmann_json::nlohmann_json)
  FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# Fetch NanoVDB (Header-only library for fast volume rendering)
if(NOT TARGET nanovdb)
  # Fetch NanoVDB
  FetchContent_Declare(
        openvdb
        URL https://github.com/AcademySoftwareFoundation/openvdb/archive/refs/tags/v12.0.0.tar.gz
    )

  set(OPENVDB_BUILD_CORE OFF CACHE BOOL "" FORCE)
  set(OPENVDB_BUILD_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
  set(OPENVDB_BUILD_BINARIES OFF CACHE BOOL "" FORCE)
  set(OPENVDB_BUILD_UNITTESTS OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(openvdb)
  add_library(nanovdb INTERFACE)
  target_include_directories(nanovdb SYSTEM INTERFACE ${openvdb_SOURCE_DIR}/nanovdb)
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

include(cmake/SkewerCoreSources.cmake)

# Find Required Dependencies for worker binaries
find_package(Imath REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(ZLIB REQUIRED)

# Create Executables
add_executable(skewer-render apps/cli/main.cc ${SKEWER_CORE_SOURCES})
add_executable(skewer-worker apps/worker/main.cc ${SKEWER_CORE_SOURCES})

# Include directories tell CMake where to look for header files
target_include_directories(skewer-render
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/external
)
target_include_directories(skewer-worker
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/external
        ${CMAKE_BINARY_DIR}
)

set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/io/scene_loader.cc" PROPERTIES
    COMPILE_OPTIONS "-fno-fast-math"
)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/io/graph_from_json.cc" PROPERTIES
    COMPILE_OPTIONS "-fno-fast-math"
)

# Link libraries
target_link_libraries(skewer-render
    PRIVATE
    nlohmann_json::nlohmann_json
    exrio::exrio
    nanovdb
)

target_link_libraries(skewer-worker
    PRIVATE
        nlohmann_json::nlohmann_json
        exrio::exrio
        nanovdb
        Imath::Imath
        OpenEXR::OpenEXR
        ZLIB::ZLIB
)

# Enable NanoVDB SIMD optimizations
target_compile_definitions(skewer-worker PRIVATE NANOVDB_USE_INTRINSICS)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(skewer-render PRIVATE -ffast-math)
    target_compile_options(skewer-worker PRIVATE -ffast-math)
endif()

if(SKEWER_BUILD_NATIVE_OPTIMIZATIONS)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        foreach(_skewer_render_target IN ITEMS skewer-render skewer-worker)
            target_compile_options(${_skewer_render_target} PRIVATE
                -O3
                # Google Cloud N2D is AMD EPYC Milan / Zen 3. Change these if the worker
                # machine type moves off Milan.
                -march=znver3
                -mtune=znver3
            )
        endforeach()
        message(STATUS "Skewer render targets tuned for Google Cloud N2D AMD EPYC Milan")
    else()
        message(STATUS
            "SKEWER_BUILD_NATIVE_OPTIMIZATIONS requested, but Milan tuning is only enabled on Linux x86_64")
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
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(_skewer_gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
      if(TARGET ${_skewer_gtest_target})
        target_compile_options(${_skewer_gtest_target} PRIVATE "-fno-fast-math")
      endif()
    endforeach()
  endif()
endif()

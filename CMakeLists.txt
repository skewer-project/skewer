cmake_minimum_required(VERSION 3.21)
project(skewer_combined LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror -std=c++20)
endif()

include(CTest)

find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

option(SKEWER_COMBINED_BUILD_EXRIO "Build exrio helper library" ON)
option(SKEWER_COMBINED_BUILD_LOOM "Build loom deep compositor" ON)
option(SKEWER_COMBINED_BUILD_SKEWER "Build skewer deep renderer" ON)
option(SKEWER_GENERATE_GOLDEN "Generate golden images in integration tests" OFF)

if(SKEWER_COMBINED_BUILD_LOOM AND NOT SKEWER_COMBINED_BUILD_EXRIO)
  message(FATAL_ERROR "SKEWER_COMBINED_BUILD_LOOM=ON requires SKEWER_COMBINED_BUILD_EXRIO=ON")
endif()

if(SKEWER_COMBINED_BUILD_SKEWER AND NOT SKEWER_COMBINED_BUILD_EXRIO)
  message(FATAL_ERROR "SKEWER_COMBINED_BUILD_SKEWER=ON requires SKEWER_COMBINED_BUILD_EXRIO=ON")
endif()

if(SKEWER_COMBINED_BUILD_EXRIO)
  add_subdirectory(libs/exrio)
endif()

# Build gRPC Proto Stubs
add_subdirectory(api)

if(SKEWER_COMBINED_BUILD_LOOM)
  add_subdirectory(loom)
endif()

if(SKEWER_COMBINED_BUILD_SKEWER)
  add_subdirectory(skewer)
endif()

# FetchContent deps (e.g. googletest) inherit directory compile options including -Werror.
# Do not treat their warnings as build failures.
if(BUILD_TESTING)
  foreach(_skewer_3p_tgt IN ITEMS gtest gtest_main gmock gmock_main)
    if(TARGET ${_skewer_3p_tgt})
      if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${_skewer_3p_tgt} PRIVATE -Wno-error)
      endif()
    endif()
  endforeach()
endif()

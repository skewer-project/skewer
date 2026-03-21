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

option(ENABLE_CLANG_TIDY "Enable clang-tidy during build" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found, but ENABLE_CLANG_TIDY is ON")
    endif()
endif()

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

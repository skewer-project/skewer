cmake_minimum_required(VERSION 3.21)
project(skewer_combined LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -std=c++20)
endif()

include(CTest)

# Place all runtime targets in a single bin/ inside the build tree
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(CMAKE_CONFIGURATION_TYPES)
    foreach(CONFIG IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${CONFIG}" CONFIG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin)
    endforeach()
endif()

option(SKEWER_COMBINED_BUILD_EXRIO "Build exrio helper library" ON)
option(SKEWER_COMBINED_BUILD_LOOM "Build loom deep compositor" ON)
option(SKEWER_COMBINED_BUILD_SKEWER "Build skewer deep renderer" ON)

if(SKEWER_COMBINED_BUILD_LOOM AND NOT SKEWER_COMBINED_BUILD_EXRIO)
    message(FATAL_ERROR "SKEWER_COMBINED_BUILD_LOOM=ON requires SKEWER_COMBINED_BUILD_EXRIO=ON")
endif()

if(SKEWER_COMBINED_BUILD_SKEWER AND NOT SKEWER_COMBINED_BUILD_EXRIO)
    message(FATAL_ERROR "SKEWER_COMBINED_BUILD_SKEWER=ON requires SKEWER_COMBINED_BUILD_EXRIO=ON")
endif()

if(SKEWER_COMBINED_BUILD_EXRIO)
    add_subdirectory(libs/exrio)
endif()

if(SKEWER_COMBINED_BUILD_LOOM)
    add_subdirectory(loom)
endif()

if(SKEWER_COMBINED_BUILD_SKEWER)
    add_subdirectory(skewer)
endif()

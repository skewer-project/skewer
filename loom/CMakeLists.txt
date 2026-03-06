cmake_minimum_required(VERSION 3.21)


project(deep_compositor_demo VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
cmake_policy(SET CMP0135 NEW)

if(NOT TARGET exrio::exrio)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libs/exrio/CMakeLists.txt")
        add_subdirectory(
            "${CMAKE_CURRENT_SOURCE_DIR}/../libs/exrio"
            "${CMAKE_CURRENT_BINARY_DIR}/exrio"
        )
    else()
        find_package(exrio REQUIRED CONFIG)
    endif()
endif()

# Loom compositor logic (composition/merging) sits here.
add_library(loom_compositor STATIC
    src/deep_compositor.cc
    src/composite_pipeline.cc
    src/deep_volume.cc
)
target_include_directories(loom_compositor PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(loom_compositor PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../skewer/external)
target_link_libraries(loom_compositor PUBLIC exrio::exrio)
target_compile_options(loom_compositor PRIVATE -Wall -Wextra -Wpedantic)

# Main executable
add_executable(loom src/main.cc)
target_link_libraries(loom PRIVATE loom_compositor)

# Worker executable
add_executable(loom-worker apps/worker/main.cc)
target_include_directories(loom-worker PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(loom-worker PRIVATE loom_compositor)

# Worker executable
add_executable(loom-worker apps/worker/main.cc)
target_include_directories(loom-worker PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(loom-worker PRIVATE loom_compositor coordinator_proto)

# Create output directory
file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/output)

target_compile_options(loom PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    include(FetchContent)
    if(NOT TARGET GTest::gtest_main)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
endif()

# Install targets
install(TARGETS loom
    RUNTIME DESTINATION bin
)

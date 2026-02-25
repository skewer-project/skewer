cmake_minimum_required(VERSION 3.15)


project(deep_compositor_demo VERSION 1.0)

# Set C++ Standard (C++17 required for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Main executable
add_executable(deep_compositor src/main.cc)
target_link_libraries(deep_compositor PRIVATE exrio::exrio)

# Create output directory
file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/output)

target_compile_options(deep_compositor PRIVATE -Wall -Wextra -Wpedantic)

# Install targets
install(TARGETS deep_compositor
    RUNTIME DESTINATION bin
)
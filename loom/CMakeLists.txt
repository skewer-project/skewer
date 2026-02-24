cmake_minimum_required(VERSION 3.15)


project(deep_compositor_demo VERSION 1.0)

# Set C++ Standard (C++17 required for modern features)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(OpenEXR REQUIRED)
find_package(Imath REQUIRED)
find_package(PNG QUIET)

# Option for PNG support
if(PNG_FOUND)
    message(STATUS "PNG support enabled")
else()
    message(STATUS "PNG support disabled (libpng not found)")
endif()

# Core library (shared between main executable, test generator, and tests)
add_library(compositor_lib STATIC
    src/utils.cpp
    src/deep_image.cpp
    src/deep_reader.cpp
    src/deep_writer.cpp
    src/deep_compositor.cpp
    src/deep_volume.cpp
)

target_include_directories(compositor_lib PUBLIC ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(compositor_lib
    OpenEXR::OpenEXR
    Imath::Imath
)

if(PNG_FOUND)
    target_compile_definitions(compositor_lib PUBLIC HAS_PNG_SUPPORT)
    target_link_libraries(compositor_lib PNG::PNG)
endif()

target_compile_options(compositor_lib PRIVATE -Wall -Wextra -Wpedantic)

# Main executable
add_executable(deep_compositor src/main.cpp)
target_link_libraries(deep_compositor compositor_lib)

# Create output directory
file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/output)

target_compile_options(deep_compositor PRIVATE -Wall -Wextra -Wpedantic)

# Install targets
install(TARGETS deep_compositor
    RUNTIME DESTINATION bin
)
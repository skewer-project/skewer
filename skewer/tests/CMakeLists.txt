# Enable CTest and define unit_tests executable
# This files specifies source files and links necessary ligraries
# Without this file, CMake wouldn't know how to compile or run tests
enable_testing()

# Locate source files (excluding main.cc)
set(TEST_SOURCES
    ../src/film/image_buffer.cc
    ../src/io/image_io.cc
)

# Create the test executable
add_executable(unit_tests
    unit/test_image_io.cc
    ${TEST_SOURCES}
)

# Include directories (same as main app)
target_include_directories(unit_tests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external
)

# Link dependencies (GTest + Project deps)
target_link_libraries(unit_tests
    PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    exrio::exrio
)

# Auto-discover tests
include(GoogleTest)
gtest_discover_tests(unit_tests)

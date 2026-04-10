# Enable CTest
# This file specifies source files and links necessary libraries
enable_testing()

# Locate source files needed for tests (relative to project root)
set(TEST_SOURCES
    skewer/src/film/image_buffer.cc
    skewer/src/film/film.cc
    skewer/src/film/deep_segment_pool.cc
    skewer/src/io/image_io.cc
)

# Create integration tests executable
add_executable(integration_tests
    integration/test_golden_image.cc
    integration/test_scene_parsing.cc
    integration/test_render_pipeline.cc
    ${TEST_SOURCES}
)

# Include directories
target_include_directories(integration_tests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/skewer/src
    ${PROJECT_SOURCE_DIR}/skewer/external
    ${PROJECT_SOURCE_DIR}/libs/exrio/include
)

# Link dependencies
target_link_libraries(integration_tests
    PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    exrio::exrio
)

# Auto-discover tests
include(GoogleTest)
gtest_discover_tests(integration_tests)
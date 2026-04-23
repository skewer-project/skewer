# Enable CTest and define unit_tests executable
# This files specifies source files and links necessary ligraries
# Without this file, CMake wouldn't know how to compile or run tests
enable_testing()

# Locate source files (excluding main.cc)
set(TEST_SOURCES
    ../src/film/image_buffer.cc
    ../src/film/film.cc
    ../src/film/deep_segment_pool.cc
    ../src/io/image_io.cc
)

set(SKEWER_SCENE_TEST_SOURCES
    ../src/scene/scene.cc
    ../src/scene/interp_curve.cc
    ../src/scene/animation.cc
    ../src/accelerators/bvh.cc
    ../src/accelerators/tlas.cc
    ../src/scene/light.cc
    ../src/io/graph_from_json.cc
    ../src/io/scene_loader.cc
    ../src/io/obj_loader.cc
    ../src/core/spectral/rgb2spec.cc
    ../src/materials/texture.cc
    ../src/materials/bsdf.cc
)

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/io/graph_from_json.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/io/scene_loader.cc"
    PROPERTIES COMPILE_OPTIONS "-fno-fast-math"
)

# Create the test executable
add_executable(unit_tests
    unit/test_image_io.cc
    unit/test_film_alpha.cc
    unit/test_quat.cc
    unit/test_trs.cc
    unit/test_interp_curve.cc
    unit/test_animation.cc
    unit/test_scene_graph.cc
    unit/test_tlas.cc
    unit/test_motion_blur.cc
    ${TEST_SOURCES}
    ${SKEWER_SCENE_TEST_SOURCES}
)

# Include directories (same as main app)
target_include_directories(unit_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/external
)

# Link dependencies (GTest + Project deps)
target_link_libraries(unit_tests
    PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    exrio::exrio
    nanovdb
)

# Auto-discover tests
include(GoogleTest)
gtest_discover_tests(unit_tests)

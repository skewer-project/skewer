if(BUILD_TESTING AND SKEWER_COMBINED_BUILD_SKEWER)
    # Enable CTest
    enable_testing()

    # Find required dependencies (available via vcpkg or system)
    find_package(Imath REQUIRED)
    find_package(OpenEXR REQUIRED)
    find_package(ZLIB REQUIRED)

    # Skewer core sources (same as used by skewer-render)
    set(SKEWER_CORE_SOURCES
        ${PROJECT_SOURCE_DIR}/skewer/src/session/render_session.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/scene/scene.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/film/deep_segment_pool.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/film/film.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/film/image_buffer.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/integrators/path_trace.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/integrators/normals.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/accelerators/bvh.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/scene/light.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/io/obj_loader.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/io/scene_loader.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/io/image_io.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/materials/bsdf.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/materials/texture.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/core/spectral/rgb2spec.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/kernels/path_kernel.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/kernels/sample_media.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/kernels/volume_dispatch.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/kernels/utils/visibility.cc
        ${PROJECT_SOURCE_DIR}/skewer/src/kernels/utils/volume_tracking.cc
    )

    # Create integration tests executable
    add_executable(integration_tests
        integration/test_golden_image.cc
        ${SKEWER_CORE_SOURCES}
    )

    # Include directories
    target_include_directories(integration_tests
        PRIVATE
        ${PROJECT_SOURCE_DIR}/skewer/src
        ${PROJECT_SOURCE_DIR}/skewer/external
        ${PROJECT_SOURCE_DIR}/libs/exrio/include
        ${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src/include
    )

    # Link dependencies
    target_link_libraries(integration_tests
        PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
        exrio::exrio
        nanovdb
        Imath::Imath
        OpenEXR::OpenEXR
        ZLIB::ZLIB
    )

    # Disable fast-math for consistency with skewer-render
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(integration_tests PRIVATE -fno-fast-math)
    endif()

    if(SKEWER_GENERATE_GOLDEN)
        target_compile_definitions(integration_tests PRIVATE SKEWER_GENERATE_GOLDEN=1)
    endif()

    # Auto-discover tests
    include(GoogleTest)
    gtest_discover_tests(integration_tests)
endif()

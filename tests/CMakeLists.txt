if(BUILD_TESTING AND SKEWER_COMBINED_BUILD_SKEWER AND SKEWER_COMBINED_BUILD_LOOM)
  # Enable CTest
  enable_testing()

  # Find required dependencies (available via vcpkg or system)
  find_package(Imath REQUIRED)
  find_package(OpenEXR REQUIRED)
  find_package(ZLIB REQUIRED)

  include("${PROJECT_SOURCE_DIR}/skewer/cmake/SkewerCoreSources.cmake")

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
        ${PROJECT_SOURCE_DIR}/loom/src
        ${PROJECT_SOURCE_DIR}/libs/exrio/include
    )

  # Link dependencies
  target_link_libraries(integration_tests
        PRIVATE
        GTest::gtest_main
        loom_compositor
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
  gtest_discover_tests(integration_tests NO_PRETTY_VALUES)
endif()

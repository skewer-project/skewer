cmake_minimum_required(VERSION 3.21)
project(exrio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
cmake_policy(SET CMP0135 NEW)

find_package(OpenEXR REQUIRED)
find_package(Imath REQUIRED)
find_package(PNG QUIET)

add_library(exrio STATIC
    src/deep_image.cc
    src/deep_reader.cc
    src/deep_writer.cc
    src/deep_compositor.cc
    src/deep_volume.cc
    src/utils.cc
)

# Alias so consumers can write exrio::exrio whether built in-tree or via find_package
add_library(exrio::exrio ALIAS exrio)

target_include_directories(exrio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(exrio
    PUBLIC
        OpenEXR::OpenEXR
        Imath::Imath
)

if(PNG_FOUND)
    target_compile_definitions(exrio PUBLIC HAS_PNG_SUPPORT)
    target_link_libraries(exrio PUBLIC PNG::PNG)
endif()

target_compile_options(exrio PRIVATE -Wall -Wextra -Wpedantic)

# ── Tests ─────────────────────────────────────────────────────────────────────

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

    file(GLOB_RECURSE EXRIO_UNIT_TEST_SOURCES tests/unit/*.cc)
    if(EXRIO_UNIT_TEST_SOURCES)
        add_executable(exrio_unit_tests ${EXRIO_UNIT_TEST_SOURCES})
        target_link_libraries(exrio_unit_tests PRIVATE exrio GTest::gtest_main)
        target_include_directories(exrio_unit_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/exrio
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        target_compile_options(exrio_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
        gtest_discover_tests(exrio_unit_tests)
    endif()

    file(GLOB_RECURSE EXRIO_INTEGRATION_TEST_SOURCES tests/integration/*.cc)
    if(EXRIO_INTEGRATION_TEST_SOURCES)
        add_executable(exrio_integration_tests ${EXRIO_INTEGRATION_TEST_SOURCES})
        target_link_libraries(exrio_integration_tests PRIVATE exrio GTest::gtest_main)
        target_include_directories(exrio_integration_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/exrio
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
        )
        target_compile_options(exrio_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
        gtest_discover_tests(exrio_integration_tests)
    endif()
endif()

# ── Install & export ──────────────────────────────────────────────────────────

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS exrio
    EXPORT exrioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/exrio
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT exrioTargets
    FILE        exrioTargets.cmake
    NAMESPACE   exrio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/exrio
)

configure_package_config_file(
    cmake/exrioConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/exrioConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/exrio
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/exrioConfigVersion.cmake
    VERSION      ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/exrioConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/exrioConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/exrio
)

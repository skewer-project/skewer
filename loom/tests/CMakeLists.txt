file(GLOB_RECURSE LOOM_UNIT_TEST_SOURCES unit/*.cc)
if(LOOM_UNIT_TEST_SOURCES)
    add_executable(loom_unit_tests ${LOOM_UNIT_TEST_SOURCES})
    target_link_libraries(loom_unit_tests PRIVATE loom_compositor GTest::gtest_main)
    target_include_directories(loom_unit_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_options(loom_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(loom_unit_tests)
endif()

file(GLOB_RECURSE LOOM_INTEGRATION_TEST_SOURCES integration/*.cc)
if(LOOM_INTEGRATION_TEST_SOURCES)
    add_executable(loom_integration_tests ${LOOM_INTEGRATION_TEST_SOURCES})
    target_link_libraries(loom_integration_tests PRIVATE loom_compositor GTest::gtest_main)
    target_include_directories(loom_integration_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_options(loom_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(loom_integration_tests)
endif()
 


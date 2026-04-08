cmake_minimum_required(VERSION 3.21)

# Static library: gRPC coordinator client loop for skewer-worker / loom-worker.
add_library(coordinator_worker STATIC src/worker_loop.cc)
target_include_directories(coordinator_worker PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(coordinator_worker PUBLIC coordinator_proto)

# ── Tests ──────────────────────────────────────────────────────────────────────
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
    include(GoogleTest)

    file(GLOB_RECURSE WORKER_UNIT_TEST_SOURCES tests/unit/*.cc)
    if(WORKER_UNIT_TEST_SOURCES)
        add_executable(worker_unit_tests ${WORKER_UNIT_TEST_SOURCES})
        target_link_libraries(worker_unit_tests PRIVATE
            coordinator_worker
            GTest::gtest_main
        )
        target_compile_options(worker_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
        gtest_discover_tests(worker_unit_tests)
    endif()
endif()

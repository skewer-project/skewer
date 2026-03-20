cmake_minimum_required(VERSION 3.21)

# Static library: gRPC coordinator client loop for skewer-worker / loom-worker.
add_library(coordinator_worker STATIC src/worker_loop.cc)
target_include_directories(coordinator_worker PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(coordinator_worker PUBLIC coordinator_proto)

cmake_minimum_required(VERSION 3.21)

# We don't find Protobuf/gRPC here directly if they are already found in the top-level
# Find them explicitly only if not already targeted
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf REQUIRED)
endif()

if(NOT TARGET gRPC::grpc++)
    find_package(gRPC REQUIRED)
endif()

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto/coordinator/v1")
set(PROTO_FILE "${PROTO_DIR}/coordinator.proto")
set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/proto/coordinator/v1/coordinator.pb.cc")
set(PROTO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/proto/coordinator/v1/coordinator.pb.h")
set(GRPC_SRC "${CMAKE_CURRENT_BINARY_DIR}/proto/coordinator/v1/coordinator.grpc.pb.cc")
set(GRPC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/proto/coordinator/v1/coordinator.grpc.pb.h")

# Ensure output directory exists before running protoc
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/proto/coordinator/v1")

add_custom_command(
    OUTPUT "${PROTO_SRC}" "${PROTO_HEADER}" "${GRPC_SRC}" "${GRPC_HEADER}"
    COMMAND protobuf::protoc
    ARGS --grpc_out="${CMAKE_CURRENT_BINARY_DIR}"
         --cpp_out="${CMAKE_CURRENT_BINARY_DIR}"
         -I "${CMAKE_CURRENT_SOURCE_DIR}"
         --plugin=protoc-gen-grpc="${grpc_cpp_plugin_location}"
         "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
)

add_library(coordinator_proto STATIC "${PROTO_SRC}" "${GRPC_SRC}")
target_include_directories(coordinator_proto PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(coordinator_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

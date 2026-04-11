# Tutorial: Integrating gRPC with C++ (Renderer Edition)

This guide explains how to generate C++ code from our `.proto` files and interface with the `RendererService`, specifically the `RenderLayer` RPC.

## 1. Prerequisites
Ensure you have the following installed:
- **Protobuf & gRPC:** (e.g., `brew install grpc` or `apt-get install libgrpc++-dev`)
- **CMake:** Version 3.16+
- **Renderer Proto:** Located at `api/proto/renderer/v1/renderer.proto`

## 2. Option A: Manual Generation (Fastest for Testing)
Run this from the project root to generate the `.pb.h` and `.pb.cc` files manually:

```bash
# Create a directory for generated files
mkdir -p internal/generated

# Generate Protobuf messages and gRPC services
protoc -I api/proto \
    --cpp_out=internal/generated \
    --grpc_out=internal/generated \
    --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
    api/proto/renderer/v1/renderer.proto
```

## 3. Option B: CMake Automation (Best for Development)
Add this to your `CMakeLists.txt` to automatically generate and link the code.

### Step 1: Find Dependencies
```cmake
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_program(_GRPC_CPP_PLUGIN_EXECUTABLE name grpc_cpp_plugin)
```

### Step 2: Define Generation Logic
```cmake
set(RENDERER_PROTO "${CMAKE_SOURCE_DIR}/api/proto/renderer/v1/renderer.proto")
set(PROTO_IMPORT_DIR "${CMAKE_SOURCE_DIR}/api/proto")
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
    OUTPUT "${GEN_DIR}/renderer/v1/renderer.pb.cc"
           "${GEN_DIR}/renderer/v1/renderer.pb.h"
           "${GEN_DIR}/renderer/v1/renderer.grpc.pb.cc"
           "${GEN_DIR}/renderer/v1/renderer.grpc.pb.h"
    COMMAND Protobuf::protoc
    ARGS --cpp_out "${GEN_DIR}"
         --grpc_out "${GEN_DIR}"
         --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
         -I "${PROTO_IMPORT_DIR}"
         "${RENDERER_PROTO}"
    DEPENDS "${RENDERER_PROTO}"
)

# Create a library for the generated code
add_library(renderer_grpc_proto
    "${GEN_DIR}/renderer/v1/renderer.pb.cc"
    "${GEN_DIR}/renderer/v1/renderer.grpc.pb.cc"
)
target_link_libraries(renderer_grpc_proto PUBLIC gRPC::grpc++ Protobuf::libprotobuf)
target_include_directories(renderer_grpc_proto PUBLIC "${GEN_DIR}")
```

### Step 3: Link to your App
```cmake
target_link_libraries(skewer-render PRIVATE renderer_grpc_proto)
```

## 4. Using the Generated Code in C++

### Including Headers
```cpp
#include "renderer/v1/renderer.pb.h"
#include "renderer/v1/renderer.grpc.pb.h"

using api::proto::renderer::v1::RenderLayerRequest;
using api::proto::renderer::v1::RenderLayerResponse;
using api::proto::renderer::v1::RendererService;
```

### Implementing the Server (Worker Side)
The Render Worker needs to implement the `RendererService::Service` interface:

```cpp
class RendererServiceImpl final : public RendererService::Service {
    grpc::Status RenderLayer(grpc::ServerContext* context, 
                             const RenderLayerRequest* request, 
                             RenderLayerResponse* response) override {
        
        std::cout << "Rendering layer: " << request->layer_name() << std::endl;
        
        // Access fields from the request
        int width = request->width();
        int height = request->height();
        
        // ... Perform Rendering ...

        // Set response fields
        response->set_success(true);
        response->set_output_uri("gs://my-bucket/output.exr");
        
        return grpc::Status::OK;
    }
};
```

### Implementing a Client (for Testing)
If you want to trigger a render from C++ (instead of the Go Coordinator):

```cpp
void TriggerRender() {
    auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    auto stub = RendererService::NewStub(channel);

    RenderLayerRequest request;
    request.set_layer_name("Background");
    request.set_width(1920);
    request.set_height(1080);

    RenderLayerResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub->RenderLayer(&context, request, &response);

    if (status.ok()) {
        std::cout << "Render successful: " << response.output_uri() << std::endl;
    } else {
        std::cerr << "RPC failed: " << status.error_message() << std::endl;
    }
}
```

## 5. Key Notes
- **Namespaces:** The generated C++ namespace follows the proto package: `api::proto::renderer::v1`.
- **Field Accessors:** Protobuf generates `field_name()` for getters and `set_field_name()` for setters. For repeated fields, use `add_field_name()`.
- **Insecure vs Secure:** For local dev, use `InsecureServerCredentials()`. Production in GKE should use SSL/TLS.

# gRPC Tutorial

In this tutorial, we will guide you through creating a simple Hello gRPC application using Golang. 

## Concepts

### gRPC

gRPC simplifies the process of building distributed systems by providing a remote procedure call (RPC) framework for communication between different software components in a distributed application. It uses Protobuf for efficient data serialization and supports bidirectional streaming. Some key things to know about gRPC include:

- **gRPC Service**: With gRPC, you start by defining a service using the proto language. The service defines the RPC methods that the client can call on the server and the expected inputs and outputs of those RPCs.
- **Code Generation**: Once you have the service definition in the protobuf, you can use the protobuf compiler to generate client-stub code and server-stub code for the service. These stubs provide necessary plumbing for communication between the client and server.
- **Client and Server Interaction**: The client can now use the generated code to make method calls on the server, as if they were regular function calls. The gRPC framework takes care of handling the network communication and marshaling/unmarshaling the data between the client and server.

### Protobuf

Protobuf is a language-agnostic binary serialization format developed by Google. It offers a concise and efficient way to define structured data schemas and serialize data for communication between different systems. Some key concepts for protobuf include:

- **Proto language**: Protobufs use a dedicated language called proto to define the structure of the data. Proto files define messages and their fields.
- **Message Types**: Protobufs work with structured data represented as messages. Messages are analogous to objects in Java/Python or structures in C/C++ and consist of named fields with assigned data types. These fields can be primitive types (e.g., int, float, boolean, string, bytes) or other message types.
- **Serialization/Deserialization**: Protobuf provides functions for serializing structured data into a binary format that is efficient for storage and transmission and for deserializing said data from binary format back to structured data after the transmission occurs.
- **Code Generation**: Once the proto files are defined, they can be compiled using the protoc compiler, which generates language-specific code.

## Prerequisites
Before diving in, let's make sure that we install the latest versions of `go` and `protoc`. This tutorial assumes you are using a Linux machine. 

## Golang
Verify your installation.
```bash
go version
```

## Protocol Buffer Compiler
- Protocol Buffer Compiler (protoc): https://grpc.io/docs/protoc-installation/
```bash
sudo apt install -y protobuf-compiler
protoc --version # Ensure compiler version is 3+
```

## Go Plugins for Protocol Buffer Compiler
Follow instructions at https://grpc.io/docs/languages/go/quickstart/
```bash
go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.35
go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.5
```
Update your `PATH` so that `protoc` can find the golang plugins:
```bash
echo 'export PATH="$PATH:$(go env GOPATH)/bin"' >> ~/.bashrc
source ~/.bashrc
```

## Introduction

Let's start by setting up a new Go project:

1. Create a new directory for your project and navigate to it:

```bash
mkdir grpc-hello-world
cd grpc-hello-world
```

2. Initialize a new Go module:

```bash
go mod init cse453/grpc-hello-world
```

3. Install the necessary gRPC dependencies:

```bash
go get -u google.golang.org/grpc
go get -u google.golang.org/protobuf/cmd/protoc-gen-go
go get -u google.golang.org/grpc/cmd/protoc-gen-go-grpc
```

Now, let's define the gRPC service using Protocol Buffers:

1. Create a new file named helloworld.proto in a new directory called proto:

```bash
mkdir proto
touch proto/helloworld.proto
```

2. Add the following content to helloworld.proto:

```proto
// Specifies the protocol buffer syntax version used in this file
syntax = "proto3";
// Specifies the Go package that will be generated for this protocol 
option go_package = "./proto/helloworld";

// Defines the package name for this protocol buffer file
package helloworld;

// Defines the RPC service and its method
service HelloWorld {
  // The RPC method SayHello takes a HelloRequest parameter and returns a HelloResponse
  rpc SayHello (HelloRequest) returns (HelloResponse);
}

// Defines the message type for the HelloRequest parameter
message HelloRequest {
  string message = 1;
}

// Defines the message type for the HelloResponse return value
message HelloResponse {
  string message = 1;
}
```

3. Compile the protobuf file:

```bash
protoc --go_out=. --go_opt=paths=source_relative \
    --go-grpc_out=. --go-grpc_opt=paths=source_relative \
    proto/helloworld.proto
```

Now let's implement the server:

1. Create a new file named `server.go`:

```bash
touch server.go
```

2. Add the following content to `server.go`:

```go
package main

import (
	"context"
	"log"
	"net"

	helloworld "cse453/grpc-hello-world/proto"

	"google.golang.org/grpc"
)

type server struct {
	helloworld.UnimplementedHelloWorldServer
}

func (s *server) SayHello(ctx context.Context, in *helloworld.HelloRequest) (*helloworld.HelloResponse, error) {
	return &helloworld.HelloResponse{Message: "Hello! " + in.GetMessage()}, nil
}

func main() {
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Failed to listen: %v", err)
	}

	grpcServer := grpc.NewServer()
	helloworld.RegisterHelloWorldServer(grpcServer, &server{})

	log.Println("gRPC server listening on :50051")
	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}
}
```

Now, let's implement the client:

1. Create a new file named `client.go`:

```bash
touch client.go
```

2. Add the following content to `client.go`:

```go
package main

import (
	"context"
	"log"

	helloworld "cse453/grpc-hello-world/proto"

	"google.golang.org/grpc"
)

func main() {
	// Creates a gRPC client connection to the server on port 50051
	conn, err := grpc.Dial(":50051", grpc.WithInsecure())

	if err != nil {
		log.Fatalf("Failed to connect: %v", err)
	}
	defer conn.Close()

	// Creates a new HelloWorldClient instance using the gRPC connection
	client := helloworld.NewHelloWorldClient(conn)

	// Creates a new HelloRequest with the message "CSE453"
	req := &helloworld.HelloRequest{Message: "CSE453"}

	// Calls the SayHello RPC method on the server with the HelloRequest as the input
	response, err := client.SayHello(context.Background(), req)
	if err != nil {
		log.Fatalf("Failed to call SayHello: %v", err)
	}

	log.Printf("Response: %s", response.Message)
}
```

Let's run the server and client!
```bash
go mod tidy # Download all the dependencies that are required in the source files
go run server.go

# In a separate terminal
go run client.go
```

If everything is set up correctly, you should see the following output on the client side:

`Response: Hello! CSE453`

Congratulations! You have successfully implemented a simple gRPC Hello application using Golang. This example can be used as a starting point to build more complex gRPC services and clients.

### How Does It Work?

gRPC effectively allows remote function calls to seem like local function
calls. To do so, it generates quite a bit of code that is run under
the hood. To help you understand how it works and where to find the
generated code, we suggest working through the following questions.

#### Questions for `server.go`

1. **RegisterHelloWorldServer**:
   - The function `RegisterHelloWorldServer` is called in `server.go`. **Where is this function defined, and what is its role in setting up the gRPC server?**
   - **Hint**: Explore the files generated by `protoc` and see if you can locate the definition of this function.

2. **HelloResponse struct**:
   - In the `SayHello` function within `server.go`, a `HelloResponse` struct is created. **Where is the `HelloResponse` struct defined?**
   - **Hint**: Investigate the generated Go files, focusing on the protocol buffer definitions.

3. **Commenting out `SayHello`**:
   - What happens if you **comment out the definition of `SayHello`** in `server.go`? Why does this happen?
   - **Hint**: Think about how the server's method implementations are tied to the interface defined in the generated files.

#### Questions for `client.go`

1. **NewHelloWorldClient**:
   - In `client.go`, the function `NewHelloWorldClient` is called. **Where is this function defined, and what does it do?**
   - **Hint**: Look in the generated files to see where client-related code is defined.

2. **HelloRequest struct**:
   - The `HelloRequest` struct is used to send a request from the client to the server. **Where is `HelloRequest` defined, and how is it structured?**
   - **Hint**: Investigate the generated code and protocol buffer definitions for this struct.

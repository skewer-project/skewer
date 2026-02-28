package main

import (
	"log"
	"net"

	"google.golang.org/grpc"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/skewer-project/skewer/internal/coordinator"
)

func main() {
	// Listen on a TCP port
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("[Error] Failed to listen: %v", err)
	}
	log.Printf("Coordinator listening on :50051")

	grpcServer := grpc.NewServer() // Generic gRPC server

	myServer := coordinator.NewServer() // Logical server

	// Register logical server with gRPC engine
	pb.RegisterCoordinatorServiceServer(grpcServer, myServer)

	// Serve the server
	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("[Error] Failed to serve: %v", err)
	}
}

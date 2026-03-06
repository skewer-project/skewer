package main

import (
	"context"
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
		log.Fatalf("[ERROR] Failed to listen: %v", err)
	}
	log.Printf("Coordinator listening on :50051")

	grpcServer := grpc.NewServer() // Generic gRPC server

	// Create dependencies
	// TODO: Make these configurable and make arguments for skewer and loom queue sizes separate
	scheduler := coordinator.NewScheduler(10000) // The max queue size for both task queues.
	tracker := coordinator.NewJobTracker()

	ctx := context.Background()

	// Create Cloud Manager (passing an empty string for local testing if credentials aren't explicitly provided yet)
	cloudManager, err := coordinator.NewK8sCloudManager(ctx, "")
	if err != nil {
		log.Fatalf("[ERROR] Failed to initialize Cloud Manager: %v", err)
	}

	myServer := coordinator.NewServer(scheduler, cloudManager, tracker) // Logical server

	// Register logical server with gRPC engine
	pb.RegisterCoordinatorServiceServer(grpcServer, myServer)

	// Serve the server
	if err := grpcServer.Serve(lis); err != nil {
		log.Fatalf("[ERROR] Failed to serve: %v", err)
	}
}

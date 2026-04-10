package main

import (
	"context"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/skewer-project/skewer/internal/coordinator"
	"google.golang.org/grpc"
)

func main() {
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("[ERROR] Failed to listen: %v", err)
	}
	log.Println("Coordinator listening on :50051")

	ctx := context.Background()

	manager, err := coordinator.NewGCPManager(ctx)
	if err != nil {
		log.Fatalf("[ERROR] Failed to initialize GCP manager: %v", err)
	}

	grpcServer := grpc.NewServer()
	pb.RegisterCoordinatorServiceServer(grpcServer, coordinator.NewServer(manager))

	go func() {
		if err := grpcServer.Serve(lis); err != nil && err != grpc.ErrServerStopped {
			log.Fatalf("[ERROR] gRPC server failed: %v", err)
		}
	}()

	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)
	<-c

	log.Println("[SERVER]: Shutting down gracefully...")
	grpcServer.GracefulStop()
	log.Println("[SERVER]: Shutdown complete.")
}

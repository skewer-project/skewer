package main

import (
	"context"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/skewer-project/skewer/internal/coordinator"
	"google.golang.org/grpc"
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
	scheduler := coordinator.NewScheduler(10000) // Increase queue size to handle large jobs without blocking
	tracker := coordinator.NewJobTracker()

	ctx := context.Background()

	go scheduler.StartSweeper(ctx, time.Hour, time.Minute)

	// Create Cloud Manager (passing an empty string for local testing if credentials aren't explicitly provided yet)
	cloudManager, err := coordinator.NewK8sCloudManager(ctx, "")
	if err != nil {
		log.Fatalf("[ERROR] Failed to initialize Cloud Manager: %v", err)
	}

	// Get local storage base path or use /data if it doesn't exist
	localStorageBase := os.Getenv("LOCAL_STORAGE_BASE")
	if localStorageBase == "" && os.Getenv("GOOGLE_APPLICATION_CREDENTIALS") == "" {
		localStorageBase = "/data"
		log.Printf("[SERVER]: No storage credentials found. Defaulting to local storage at: %s", localStorageBase)
	}

	myServer := coordinator.NewServer(scheduler, cloudManager, tracker, localStorageBase) // Logical server

	// Register logical server with gRPC engine
	pb.RegisterCoordinatorServiceServer(grpcServer, myServer)

	// Serve the server as a background goroutine
	go func() {
		if err := grpcServer.Serve(lis); err != nil {
			log.Fatalf("[ERROR] Failed to serve: %v", err)
		}
	}()

	// Wait for interrupt signal to gracefully shut down the server
	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM) // SIGINT (Ctrl+C) and SIGTERM (Docker/K8s stop)

	<-c // This will block until the signal is received

	// Stop background capacity manager loop and grpc server
	log.Println("[SERVER] Shutting down gracefully...")
	myServer.Stop()
	grpcServer.GracefulStop()
	log.Println("[SERVER] Shutdown complete.")
}

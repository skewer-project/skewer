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
	"github.com/skewer-project/skewer/orchestration/internal/coordinator"
	"google.golang.org/grpc"
)

func main() {
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("[ERROR] Failed to listen: %v", err)
	}
	log.Printf("Coordinator listening on :50051")

	grpcServer := grpc.NewServer()

	scheduler := coordinator.NewScheduler(10000)
	tracker := coordinator.NewJobTracker()

	ctx := context.Background()
	go scheduler.StartSweeper(ctx, time.Hour, time.Minute)

	cloudManager, err := coordinator.NewK8sCloudManager(ctx, "")
	if err != nil {
		log.Fatalf("[ERROR] Failed to initialize Cloud Manager: %v", err)
	}

	localStorageBase := os.Getenv("LOCAL_STORAGE_BASE")
	if localStorageBase == "" && os.Getenv("GOOGLE_APPLICATION_CREDENTIALS") == "" {
		localStorageBase = "/data"
		log.Printf("[SERVER]: No storage credentials found. Defaulting to local storage at: %s", localStorageBase)
	}

	myServer := coordinator.NewServer(scheduler, cloudManager, tracker, localStorageBase)

	pb.RegisterCoordinatorServiceServer(grpcServer, myServer)

	go func() {
		if err := grpcServer.Serve(lis); err == grpc.ErrServerStopped {
			grpcServer.GracefulStop()
		}
	}()

	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)

	<-c

	log.Println("[SERVER]: Shutting down gracefully...")
	myServer.Stop()
	grpcServer.GracefulStop()
	log.Println("[SERVER]: Shutdown complete.")
}

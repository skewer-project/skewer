package main

import (
	"context"
	"log"
	"log/slog"
	"net"
	"os"
	"os/signal"
	"syscall"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/skewer-project/skewer/internal/coordinator"
	"google.golang.org/grpc"
)

func main() {
	slog.SetDefault(slog.New(slog.NewJSONHandler(os.Stdout, nil)))

	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("[ERROR] Failed to listen: %v", err)
	}
	slog.Info("coordinator listening", "addr", ":50051")

	grpcServer := grpc.NewServer()

	coordAddr := os.Getenv("COORDINATOR_GRPC_ADDR")
	if coordAddr == "" {
		coordAddr = "skewer-coordinator.default.svc.cluster.local:50051"
	}

	ctx := context.Background()

	cloudManager, err := coordinator.NewCloudRunManager(ctx)
	if err != nil {
		log.Fatalf("[ERROR] Failed to initialize Cloud Run manager: %v", err)
	}
	defer func() { _ = cloudManager.Close() }()

	scheduler := coordinator.NewScheduler(cloudManager, coordAddr)
	tracker := coordinator.NewJobTracker()

	myServer := coordinator.NewServer(scheduler, tracker)

	pb.RegisterCoordinatorServiceServer(grpcServer, myServer)

	go func() {
		if err := grpcServer.Serve(lis); err == grpc.ErrServerStopped {
			grpcServer.GracefulStop()
		}
	}()

	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)

	<-c

	slog.Info("shutting down gracefully")
	myServer.Stop()
	grpcServer.GracefulStop()
	slog.Info("shutdown complete")
}

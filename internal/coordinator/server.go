package coordinator

import (
	"context"
	"log"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

// A server to implement gRPC interface
type Server struct {
	pb.UnimplementedCoordinatorServiceServer
}

func NewServer() *Server {
	return &Server{}
}

// Submit a job (stub)
func (s *Server) SubmitJob(ctx context.Context, req *pb.SubmitJobRequest) (*pb.SubmitJobResponse, error) {
	jobId := "job-123-stub"
	log.Printf("Received job submission: %v", req)

	return &pb.SubmitJobResponse{
		JobId: jobId,
	}, nil
}

// Register a worker (stub)
func (s *Server) RegisterWorker(ctx context.Context, req *pb.RegisterWorkerRequest) (*pb.RegisterWorkerResponse, error) {
	workerId := "worker-123-stub"
	log.Printf("Worker registering: %v", req)

	return &pb.RegisterWorkerResponse{
		WorkerId: workerId,
	}, nil
}

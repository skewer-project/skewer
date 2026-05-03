package coordinator

import (
	"context"
	"strings"

	"github.com/google/uuid"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Server implements the CoordinatorService gRPC interface.
// It is stateless: all pipeline state lives in Cloud Workflows executions.
type Server struct {
	pb.UnimplementedCoordinatorServiceServer
	manager *GCPManager
}

func NewServer(manager *GCPManager) *Server {
	return &Server{manager: manager}
}

func (s *Server) SubmitPipeline(ctx context.Context, req *pb.SubmitPipelineRequest) (*pb.SubmitPipelineResponse, error) {
	if req.SceneUri == "" {
		return nil, status.Error(codes.InvalidArgument, "scene_uri is required")
	}
	if !strings.HasPrefix(req.SceneUri, "gs://") {
		return nil, status.Error(codes.InvalidArgument, "scene_uri must start with gs://")
	}
	if req.CompositeOutputUriPrefix == "" {
		return nil, status.Error(codes.InvalidArgument, "composite_output_uri_prefix is required")
	}
	if !strings.HasPrefix(req.CompositeOutputUriPrefix, "gs://") {
		return nil, status.Error(codes.InvalidArgument, "composite_output_uri_prefix must start with gs://")
	}

	if req.PipelineId == "" {
		req.PipelineId = "p-" + uuid.New().String()
	}

	execName, err := s.manager.ExecutePipeline(ctx, req)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "start pipeline: %v", err)
	}

	return &pb.SubmitPipelineResponse{PipelineId: execName}, nil
}

func (s *Server) GetPipelineStatus(ctx context.Context, req *pb.GetPipelineStatusRequest) (*pb.GetPipelineStatusResponse, error) {
	if req.PipelineId == "" {
		return nil, status.Error(codes.InvalidArgument, "pipeline_id is required")
	}

	resp, err := s.manager.GetPipelineStatus(ctx, req.PipelineId)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "get pipeline status: %v", err)
	}

	return resp, nil
}

func (s *Server) CancelPipeline(ctx context.Context, req *pb.CancelPipelineRequest) (*pb.CancelPipelineResponse, error) {
	if req.PipelineId == "" {
		return nil, status.Error(codes.InvalidArgument, "pipeline_id is required")
	}

	if err := s.manager.CancelPipeline(ctx, req.PipelineId); err != nil {
		return nil, status.Errorf(codes.Internal, "cancel pipeline: %v", err)
	}

	return &pb.CancelPipelineResponse{Success: true}, nil
}

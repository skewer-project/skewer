package coordinator

import (
	"context"
	"fmt"

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
	if len(req.Layers) == 0 {
		return nil, status.Error(codes.InvalidArgument, "at least one layer is required")
	}
	if req.NumFrames <= 0 {
		return nil, status.Error(codes.InvalidArgument, "num_frames must be positive")
	}
	if req.CompositeOutputUriPrefix == "" {
		return nil, status.Error(codes.InvalidArgument, "composite_output_uri_prefix is required")
	}
	for _, l := range req.Layers {
		if l.LayerId == "" {
			return nil, status.Error(codes.InvalidArgument, "each layer must have a non-empty layer_id")
		}
		if l.SceneUri == "" {
			return nil, status.Error(codes.InvalidArgument, fmt.Sprintf("layer %q is missing scene_uri", l.LayerId))
		}
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

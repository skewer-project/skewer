package coordinator

import (
	"context"
	"fmt"
	"log"
	"net/url"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

// Server implements the gRPC CoordinatorService
type Server struct {
	pb.UnimplementedCoordinatorServiceServer
	scheduler *Scheduler
	manager   *CloudManager
	tracker   *JobTracker
}

func NewServer(scheduler *Scheduler, manager *CloudManager, tracker *JobTracker) *Server {
	return &Server{
		scheduler: scheduler,
		manager:   manager,
		tracker:   tracker,
	}
}

// ================= //
// * EXTERNAL API *  //
// ================= //

func (s *Server) SubmitJob(ctx context.Context, req *pb.SubmitJobRequest) (*pb.SubmitJobResponse, error) {
	jobID := req.JobId // May make this a coordinator-generated ID
	log.Printf("[COORDINATOR] Received SubmitJob request: %s", jobID)

	var newJob Job

	// Route the job creation based on the user's requested payload
	switch jobTypes := req.JobType.(type) {
	case *pb.SubmitJobRequest_RenderJob:
		newJob = &RenderJob{
			JobID:          jobID,
			Dependencies:   req.DependsOn,
			Status:         pb.GetJobStatusResponse_JOB_STATUS_UNSPECIFIED,
			TotalTasks:     req.NumFrames * jobTypes.RenderJob.SampleDivision,
			SampleDivision: jobTypes.RenderJob.SampleDivision,
			OriginalReq:    req,
		}
	case *pb.SubmitJobRequest_CompositeJob:
		newJob = &CompositeJob{
			JobID:        jobID,
			Dependencies: req.DependsOn,
			Status:       pb.GetJobStatusResponse_JOB_STATUS_UNSPECIFIED,
			TotalFrames:  req.NumFrames,
			OriginalReq:  req,
		}
	default:
		return nil, fmt.Errorf("[ERROR] Unknown job type provided.")
	}

	// Add the job to the JobTracker
	if err := s.tracker.AddJob(newJob); err != nil {
		return nil, err
	}

	// ONLY queue the tasks if there are NO dependencies!
	if len(req.DependsOn) == 0 {
		var err error
		switch req.JobType.(type) {
		case *pb.SubmitJobRequest_RenderJob:
			err = s.handleRenderJobSubmit(jobID, req, req.GetRenderJob())
		case *pb.SubmitJobRequest_CompositeJob:
			err = s.handleCompositeJobSubmit(jobID, req, req.GetCompositeJob())
		}

		if err != nil {
			return nil, err
		}
	}

	return &pb.SubmitJobResponse{JobId: jobID}, nil
}

func (s *Server) GetJobStatus(ctx context.Context, req *pb.GetJobStatusRequest) (*pb.GetJobStatusResponse, error) {
	// Look up JobID in the JobTracker (if it exists)
	job, err := s.tracker.GetJob(req.JobId)
	errResponse := ""
	if err != nil {
		errResponse = err.Error()
	}

	// Return response. Interface handles progress automatically
	return &pb.GetJobStatusResponse{
		JobStatus:       job.GetStatus(),
		ProgressPercent: job.Progress() * 100,
		ErrorMessage:    errResponse,
	}, nil
}

func (s *Server) CancelJob(ctx context.Context, req *pb.CancelJobRequest) (*pb.CancelJobResponse, error) {
	// Use the JobTracker's CancelJob function
	err := s.tracker.CancelJob(req.JobId)

	if err != nil {
		return &pb.CancelJobResponse{
			Success: false,
		}, nil
	}

	// Flush any pending tasks for this job from the Scheduler queue
	s.scheduler.PurgeJobTasks(req.JobId)

	return &pb.CancelJobResponse{
		Success: true,
	}, nil
}

// =====================================================================
// INTERNAL API (Called by GKE Workers)
// =====================================================================

// GetWorkStream - The KEY KEDA Pull Endpoint
// Workers call this ONCE and hold the stream open to rapidly pull tasks.
func (s *Server) GetWorkStream(req *pb.GetWorkStreamRequest, stream pb.CoordinatorService_GetWorkStreamServer) error {
	workerID := req.WorkerId
	capabilities := req.Capabilities
	log.Printf("Worker %s connected. Capabilities: %v", workerID, capabilities)

	for {
		// Block and wait for the Scheduler to hand us a task.
		// TODO: Update scheduler.GetNextTask to accept `capabilities` so it only
		// hands Loom tasks to Loom workers, and Skewer tasks to Skewer workers.
		task, err := s.scheduler.GetNextTask(stream.Context(), capabilities)
		if err != nil {
			// If the context is cancelled (worker disconnected), exit cleanly
			log.Printf("Worker %s stream closed: %v", workerID, err)
			return err
		}

		// Package task into the Protobuf format.
		workPackage := &pb.WorkPackage{
			JobId:  task.JobID,
			TaskId: task.ID,
			// FrameId: task.FrameID, // Assuming you added this to your Task struct
		}

		// Type-assert the payload and map it to the Protobuf 'oneof'
		switch t := task.Payload.(type) {
		case *pb.RenderTask:
			workPackage.Payload = &pb.WorkPackage_RenderTask{RenderTask: t}
		case *pb.MergeTask:
			workPackage.Payload = &pb.WorkPackage_MergeTask{MergeTask: t}
		case *pb.CompositeTask:
			workPackage.Payload = &pb.WorkPackage_CompositeTask{CompositeTask: t}
		default:
			log.Printf("Critical: Unknown task payload type for task %s", task.ID)
			continue
		}

		// Send workPackage down the wire
		if err := stream.Send(workPackage); err != nil {
			log.Printf("Failed to send task to worker %s: %v", workerID, err)
			// The worker dropped offline exactly as we tried to send.
			// Immediately requeue the task so it isn't lost.
			s.scheduler.RequeueTask(task.ID)
			return err
		}

		log.Printf("Assigned task %s to worker %s", task.ID, workerID)
	}
}

func (s *Server) ReportTaskResult(ctx context.Context, req *pb.ReportTaskResultRequest) (*pb.ReportTaskResultResponse, error) {
	taskID := req.GetTaskId()
	jobID := req.GetJobId()

	log.Printf("Task %s completed by worker %s: success=%v", taskID, req.WorkerId, req.Success)

	// 1. Tell the scheduler the worker is officially done with it (stops the Sweeper timeout)
	s.scheduler.MarkTaskComplete(taskID)

	if !req.Success {
		// TODO: Handle failure. Requeue the task, or increment a failure counter
		// in your JobTracker and fail the whole job if it exceeds max retries.
		return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
	}

	// 2. Update the DAG state
	// TODO: Implement the logic to update your JobTracker
	// e.g., tracker.MarkChunkComplete(jobID, req.FrameId)

	// 3. Queue downstream dependencies
	// TODO: If that was the final Skewer chunk for Frame 5, queue the Loom MergeTask
	// TODO: If that was the final frame for the Job, check if a dependent CompositeJob needs to wake up

	return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
}

// =====================================================================
// STUBBED HELPERS (Business Logic)
// =====================================================================

func (s *Server) handleRenderJobSubmit(jobID string, req *pb.SubmitJobRequest, params *pb.RenderJobParams) error {
	// TODO: Use your math logic here!
	// Loop over req.NumFrames.
	// Loop over params.SampleDivision.
	// Create *pb.RenderTask objects.
	// Call s.scheduler.EnqueueTask(...) for each chunk.
	return nil
}

func (s *Server) handleCompositeJobSubmit(jobID string, req *pb.SubmitJobRequest, params *pb.CompositeJobParams) error {
	// TODO: Loop over req.NumFrames.
	// Create *pb.CompositeTask objects.
	// Call s.scheduler.EnqueueTask(...) for each frame.
	for _, frame := range req. {
		task := &pb.CompositeTask{
			JobId:    jobID,
			FrameId:  frame,
			NumTasks: params.NumTasks,
		}
		s.scheduler.EnqueueTask(task, jobID, frame)
	}
	return nil
}

// func generateJobID() string {
// 	return "job-" + time.Now().Format("20060102150405")
// }

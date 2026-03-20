package coordinator

import (
	"context"
	"fmt"
	"log"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

// Server implements the gRPC CoordinatorService
type Server struct {
	pb.UnimplementedCoordinatorServiceServer
	scheduler        *Scheduler
	manager          CloudManager
	tracker          *JobTracker
	localStorageBase string
	cancel           context.CancelFunc
}

func NewServer(scheduler *Scheduler, manager CloudManager, tracker *JobTracker, localStorageBase string) *Server {
	s := &Server{
		scheduler:        scheduler,
		manager:          manager,
		tracker:          tracker,
		localStorageBase: localStorageBase,
	}

	// Start a background loop to manage worker capacity
	ctx, cancel := context.WithCancel(context.Background())
	s.cancel = cancel // cancellation context for server
	go s.runCapacityManager(ctx)

	// Set the terminal failure callback
	s.scheduler.OnTaskFailedPermanently = func(task *Task) {
		log.Printf("[SERVER] Task %s for job %s failed permanently. Failing job.", task.ID, task.JobID)
		s.tracker.CancelJob(task.JobID)
		s.scheduler.PurgeJobTasks(task.JobID)
	}

	return s
}

// Method to gracefully stop server
func (s *Server) Stop() {
	if s.cancel != nil {
		s.cancel()
	}
}

// ================= //
// * EXTERNAL API *  //
// ================= //

func (s *Server) SubmitJob(ctx context.Context, req *pb.SubmitJobRequest) (*pb.SubmitJobResponse, error) {
	jobID := req.JobId
	if jobID == "" {
		jobID = uuid.New().String()
	}

	log.Printf("[COORDINATOR] Received SubmitJob request: %s", jobID)

	var newJob Job

	// Route the job creation based on the user's requested payload
	switch req.JobType.(type) {
	case *pb.SubmitJobRequest_RenderJob:
		newJob = &RenderJob{
			JobID:        jobID,
			Dependencies: req.DependsOn,
			Status:       pb.GetJobStatusResponse_JOB_STATUS_UNSPECIFIED,
			TotalTasks:   req.NumFrames, // Strictly one task per frame
			OriginalReq:  req,
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
	if err != nil {
		return &pb.GetJobStatusResponse{
			JobStatus:    pb.GetJobStatusResponse_JOB_STATUS_FAILED,
			ErrorMessage: fmt.Sprintf("Job not found: %v", err),
		}, nil
	}

	// Return response. Interface handles progress automatically
	return &pb.GetJobStatusResponse{
		JobStatus:       job.GetStatus(),
		ProgressPercent: job.Progress() * 100,
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
// Workers call this to pull a SINGLE task. We close the stream after one task
// to ensure fair distribution and prevent a single worker from grabbing the entire queue.
func (s *Server) GetWorkStream(req *pb.GetWorkStreamRequest, stream pb.CoordinatorService_GetWorkStreamServer) error {
	workerID := req.WorkerId
	capabilities := req.Capabilities
	log.Printf("[COORDINATOR] Worker %s connected. Capabilities: %v", workerID, capabilities)

	// Block and wait for the Scheduler to hand us a task.
	task, err := s.scheduler.GetNextTask(stream.Context(), capabilities)
	if err != nil {
		// If the context is cancelled (worker disconnected), exit cleanly
		log.Printf("[SERVER]: Worker %s stream closed: %v", workerID, err)
		return err
	}

	// Lazy cancellation check
	job, err := s.tracker.GetJob(task.JobID)

	// If the job is marked as FAILED or was deleted, throw this task in the trash!
	if err != nil || job.GetStatus() == pb.GetJobStatusResponse_JOB_STATUS_FAILED {
		log.Printf("[COORDINATOR]: Skipping cancelled/deleted task %s for job %s", task.ID, task.JobID)
		s.scheduler.MarkTaskComplete(task.ID) // Remove from active memory
		return nil                            // Close stream, worker will reconnect and try again
	}

	// Package task into the Protobuf format.
	workPackage := &pb.WorkPackage{
		JobId:   task.JobID,
		TaskId:  task.ID,
		FrameId: task.FrameID,
	}

	// Type-assert the payload and map it to the Protobuf 'oneof'
	switch t := task.Payload.(type) {
	case *pb.RenderTask:
		workPackage.Payload = &pb.WorkPackage_RenderTask{RenderTask: t}
	case *pb.CompositeTask:
		workPackage.Payload = &pb.WorkPackage_CompositeTask{CompositeTask: t}
	default:
		log.Printf("[ERROR]: Unknown task payload type for task %s", task.ID)
		return nil
	}

	// Send workPackage down the wire
	if err := stream.Send(workPackage); err != nil {
		log.Printf("[ERROR]: Failed to send task to worker %s: %v", workerID, err)
		s.scheduler.RequeueTask(task.ID)
		return err
	}

	log.Printf("[COORDINATOR]: Assigned task %s to worker %s", task.ID, workerID)
	return nil // Close the stream after one task to ensure fair distribution
}

func (s *Server) ReportTaskResult(ctx context.Context, req *pb.ReportTaskResultRequest) (*pb.ReportTaskResultResponse, error) {
	taskID := req.GetTaskId()
	jobID := req.GetJobId()

	log.Printf("[COORDINATOR]: Task %s completed by worker %s: success=%v", taskID, req.WorkerId, req.Success)

	// Tell the scheduler the worker is officially done with it (stops the Sweeper timeout)
	task, exists := s.scheduler.Task(taskID)
	if !exists {
		log.Printf("[ERROR]: Task %s not found in active tasks", taskID)
		return &pb.ReportTaskResultResponse{Acknowledged: false}, fmt.Errorf("[ERROR]: Task %s not found in active tasks", taskID)
	}

	if !req.Success {
		// Handle failure. Requeue the task, or increment a failure counter
		// in the JobTracker and fail the whole job if it exceeds max retries.
		task.Retries++
		if task.Retries > 3 {
			log.Printf("[COORDINATOR]: Task %s failed too many times. Failing Job %s", taskID, jobID)

			// Mark the job as failed
			err := s.tracker.CancelJob(jobID)
			if err != nil {
				log.Printf("[ERROR]: Failed to retrieve job %s to mark as failed (%v)", jobID, err)
				return &pb.ReportTaskResultResponse{Acknowledged: false}, err
			}

			s.scheduler.PurgeJobTasks(jobID) // Stop other tasks for this job
		} else {
			s.scheduler.RequeueTask(taskID) // Requeue and try again otherwise
		}

		return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
	}

	s.scheduler.MarkTaskComplete(taskID)

	// Update completed Tasks for the job
	job, err := s.tracker.GetJob(task.JobID)
	if err != nil {
		return &pb.ReportTaskResultResponse{Acknowledged: false}, err
	}

	var complete bool
	switch jobType := job.(type) {
	case *RenderJob:
		jobType.mu.Lock()

		// Update completed tasks and check if job is complete
		jobType.CompletedTasks++
		complete = jobType.CompletedTasks == jobType.TotalTasks

		jobType.mu.Unlock()

	case *CompositeJob:
		jobType.mu.Lock()
		jobType.CompletedFrames++
		complete = jobType.CompletedFrames == jobType.TotalFrames
		jobType.mu.Unlock()
	}

	// Queue downstream dependencies if job is complete
	if complete {
		job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_COMPLETED)

		// Ask the tracker to safely update the math and report unlocked dependencies
		unlockedJobs := s.tracker.UnlockDependencies(job.ID())

		// Loop through whatever jobs just hit 0 dependencies and queue them
		for _, newJob := range unlockedJobs {
			// (We will eventually call s.handleCompositeJobSubmit here
			// to actually queue the tasks for the newly unlocked job)
			log.Printf("[COORDINATOR]: Job %s is fully unlocked and ready to queue!", newJob.ID())

			// Type assert to figure out what kind of job just unlocked and queue it
			req := newJob.GetOriginalReq()
			switch typedJob := newJob.(type) {
			case *RenderJob:
				s.handleRenderJobSubmit(typedJob.ID(), req, req.GetRenderJob())
			case *CompositeJob:
				s.handleCompositeJobSubmit(typedJob.ID(), req, req.GetCompositeJob())
			}
		}
	}

	return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
}

// =====================================================================
// STUBBED HELPERS
// =====================================================================

func (s *Server) runCapacityManager(ctx context.Context) {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			queueLengths := s.scheduler.GetQueueLengths()
			activeCounts := s.scheduler.GetActiveTaskCounts()

			// Determine global limit (default to 1 in local mode, 20 in cloud)
			limit := 20
			if s.localStorageBase != "" {
				limit = 1 // LOCAL MODE: Avoid blowing up laptop
			}
			if val, err := strconv.Atoi(os.Getenv("MAX_WORKERS")); err == nil {
				limit = val
			}

			// Handle skewer-worker
			skewerTarget := queueLengths["skewer"] + activeCounts["skewer"]
			if skewerTarget > limit {
				skewerTarget = limit
			}
			if queueLengths["skewer"] == 0 && activeCounts["skewer"] == 0 {
				skewerTarget = 0
			}
			if err := s.manager.EnsureCapacity(ctx, "skewer-worker", skewerTarget); err != nil {
				log.Printf("[SERVER]: Failed to ensure skewer capacity: %v", err)
			}

			// Handle loom-worker
			loomTarget := queueLengths["loom"] + activeCounts["loom"]
			loomTarget = min(loomTarget, limit)
			if queueLengths["loom"] == 0 && activeCounts["loom"] == 0 {
				loomTarget = 0
			}
			if err := s.manager.EnsureCapacity(ctx, "loom-worker", loomTarget); err != nil {
				log.Printf("[SERVER]: Failed to ensure loom capacity: %v", err)
			}
		}
	}
}

func (s *Server) translateLocalPath(uri string) (string, error) {
	if s.localStorageBase == "" {
		return uri, nil
	}

	// Handle Cloud URIs (gs://)
	if trimmed, hasPrefix := strings.CutPrefix(uri, "gs://"); hasPrefix {
		parts := strings.SplitN(trimmed, "/", 2)
		if len(parts) == 1 {
			return filepath.Join(s.localStorageBase, parts[0]), nil
		}
		return filepath.Join(s.localStorageBase, parts[0], parts[1]), nil
	}

	// Handle Local Paths (Mapping host paths to container paths)
	hostDataPath := os.Getenv("LOCAL_DATA_PATH")

	// If the path is already absolute and starts with the host's data path, swap it.
	if hostDataPath != "" && filepath.IsAbs(uri) && strings.HasPrefix(uri, hostDataPath) {
		rel, _ := filepath.Rel(hostDataPath, uri)
		// SECURITY: Clean the path and prevent traversal
		cleanRel := filepath.Clean(rel)
		if strings.HasPrefix(cleanRel, "..") {
			return "", fmt.Errorf("[ERROR]: Blocked dangerous traversal to uri: %s", uri) // Will propogate and be rejected by job submit
		}

		return filepath.Join(s.localStorageBase, cleanRel), nil
	}

	// If the path is relative and starts with "data/", swap it for our storage base.
	if trimmed, hasPrefix := strings.CutPrefix(uri, "data/"); hasPrefix {
		// SECURITY: Clean the path and prevent traversal
		cleanTrimmed := filepath.Clean(trimmed)
		if strings.HasPrefix(cleanTrimmed, "..") || strings.HasPrefix(cleanTrimmed, "/") {
			return "", fmt.Errorf("[ERROR]: Blocked dangerous traversal to uri: %s", uri) // Will propogate and be rejected by job submit
		}
		return filepath.Join(s.localStorageBase, cleanTrimmed), nil
	}

	// Repo-relative paths (e.g. scenes/foo.json): workspace root is mounted at
	// localStorageBase (/data in Kubernetes). Without this, workers see a bare
	// relative path and resolve it from process CWD (/) and the task fails.
	if !filepath.IsAbs(uri) {
		cleanRel := filepath.Clean(uri)
		if cleanRel == "." || cleanRel == "" {
			return "", fmt.Errorf("[ERROR]: Invalid empty relative path")
		}
		if strings.HasPrefix(cleanRel, "..") {
			return "", fmt.Errorf("[ERROR]: Blocked dangerous traversal to uri: %s", uri)
		}
		return filepath.Join(s.localStorageBase, cleanRel), nil
	}

	return uri, nil
}

func (s *Server) handleRenderJobSubmit(jobID string, req *pb.SubmitJobRequest, job *pb.RenderJob) error {
	for frameID := int32(0); frameID < req.GetNumFrames(); frameID++ {
		// Calculate uri prefixes
		outputUriPrefix, err := s.translateLocalPath(job.GetOutputUriPrefix())
		if err != nil {
			return err
		}
		sceneUri, err := s.translateLocalPath(job.GetSceneUri())
		if err != nil {
			return err
		}
		if frameID == 0 {
			log.Printf("[COORDINATOR] Scene path: raw=%q -> worker=%q", job.GetSceneUri(), sceneUri)
		}

		// Replace #### with padded frame ID if it exists in the scene URI
		frameSceneUri := strings.ReplaceAll(sceneUri, "####", fmt.Sprintf("%04d", frameID+1))

		// Some deep checking for the final file type extension
		extension := ".exr"
		if !job.GetEnableDeep() {
			extension = ".png"
		}

		// This is the output uri for the frame
		outputUri, err := url.JoinPath(outputUriPrefix, fmt.Sprintf("frame-%04d%s", frameID+1, extension))
		if err != nil {
			return err
		}

		// Create render task to enqueue it to the scheduler
		task := &pb.RenderTask{
			SceneUri: frameSceneUri,
			Width:    req.GetWidth(),
			Height:   req.GetHeight(),

			OutputUri:  outputUri,
			EnableDeep: job.GetEnableDeep(),
			Threads:    job.GetThreads(),

			NoiseThreshold: job.GetNoiseThreshold(),
			MinSamples:     job.GetMinSamples(),
			AdaptiveStep:   job.GetAdaptiveStep(),
			MaxSamples:     job.GetMaxSamples(),
		}

		if _, err := s.scheduler.EnqueueTask(task, jobID, fmt.Sprint(frameID)); err != nil {
			return fmt.Errorf("[ERROR]: Failed to enqueue render task for job %s frame %d (%w)", jobID, frameID, err)
		}
	}

	return nil
}

func (s *Server) handleCompositeJobSubmit(jobID string, req *pb.SubmitJobRequest, job *pb.CompositeJob) error {

	outputUriPrefix, err := s.translateLocalPath(job.GetOutputUriPrefix())
	if err != nil {
		return err
	}

	for frameID := 0; frameID < int(req.GetNumFrames()); frameID++ {

		// gs://bucket/renders/smoke
		originalPrefixes := job.GetLayerUriPrefixes()

		// Create a fresh slice for THIS specific frame task
		frameLayerUris := make([]string, len(originalPrefixes))

		// Clean all the uri prefixes for the layers and store them as full uris in new list
		for idx, uriPrefix := range originalPrefixes {
			translatedPrefix, err := s.translateLocalPath(uriPrefix)
			if err != nil {
				return err
			}

			fullLayerUri, err := url.JoinPath(translatedPrefix, fmt.Sprintf("frame-%04d.exr", frameID+1))
			if err != nil {
				return err
			}
			frameLayerUris[idx] = fullLayerUri // Safe!
		}

		// Finally safely join path
		finalOutputUri, err := url.JoinPath(outputUriPrefix, fmt.Sprintf("frame-%04d.exr", frameID+1))
		if err != nil {
			return err
		}

		task := &pb.CompositeTask{
			LayerUris: frameLayerUris,

			Width:  req.GetWidth(),
			Height: req.GetHeight(),

			OutputUri: finalOutputUri,
		}
		s.scheduler.EnqueueTask(task, jobID, fmt.Sprint(frameID))

	}

	return nil
}

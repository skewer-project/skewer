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
		log.Printf("[SERVER]: Task %s for job %s failed permanently. Failing job.", task.ID, task.JobID)
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

	log.Printf("[COORDINATOR]: Received SubmitJob request: %s", jobID)

	var newJob Job

	// Try to resolve general job info from dependencies if missing
	numFrames := req.NumFrames
	width := req.Width
	height := req.Height

	if (numFrames == 0 || width == 0 || height == 0) && len(req.DependsOn) > 0 {
		for _, depID := range req.DependsOn {
			depJob, err := s.tracker.GetJob(depID)
			if err == nil {
				depReq := depJob.GetOriginalReq()

				// Validation: Ensure dimensions match if we already have a resolved width/height
				if width > 0 && depReq.Width > 0 && width != depReq.Width {
					return nil, fmt.Errorf("[ERROR] Dimension mismatch: dependency %s is %dx%d but previous deps are %dx%d", depID, depReq.Width, depReq.Height, width, height)
				}

				// Take the max frames found across all dependencies
				if numFrames < depReq.NumFrames {
					numFrames = depReq.NumFrames
				}
				// Take the largest dimensions found across all dependencies (if not already set)
				if width == 0 {
					width = depReq.Width
				}
				if height == 0 {
					height = depReq.Height
				}
			}
		}
		// Update the request object so handlers use the inherited values
		req.NumFrames = numFrames
		req.Width = width
		req.Height = height
	}

	// Route the job creation based on the user's requested payload
	switch req.JobType.(type) {
	case *pb.SubmitJobRequest_RenderJob:
		newJob = &RenderJob{
			JobID:        jobID,
			Dependencies: req.DependsOn,
			Status:       pb.GetJobStatusResponse_JOB_STATUS_UNSPECIFIED,
			TotalTasks:   numFrames, // Strictly one task per frame
			OriginalReq:  req,
		}
	case *pb.SubmitJobRequest_CompositeJob:
		newJob = &CompositeJob{
			JobID:        jobID,
			Dependencies: req.DependsOn,
			Status:       pb.GetJobStatusResponse_JOB_STATUS_UNSPECIFIED,
			TotalFrames:  numFrames,
			OriginalReq:  req,
		}
	default:
		return nil, fmt.Errorf("[ERROR]: Unknown job type provided.")
	}

	// Auto-append unique identifier to output path if it's the default to prevent collisions
	currentPrefix := newJob.GetOutputPrefix()
	if currentPrefix == "data/renders/" || currentPrefix == "data/renders" {
		suffix := req.JobName
		if suffix == "" || suffix == "skewer-job" || suffix == "skewer-composite" {
			suffix = jobID[:8] // Use first 8 chars of UUID
		}
		newPrefix := filepath.Join(currentPrefix, suffix)
		newJob.SetOutputPrefix(newPrefix)
		log.Printf("[COORDINATOR] Auto-set output path for job %s: %s", jobID, newPrefix)
	}

	// Add the job to the JobTracker
	if err := s.tracker.AddJob(newJob); err != nil {
		return nil, err
	}

	// ONLY queue the tasks if the job is ready (no pending dependencies)
	if newJob.GetStatus() == pb.GetJobStatusResponse_JOB_STATUS_QUEUED {
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

	// Update job status to RUNNING if it's currently QUEUED
	if job.GetStatus() == pb.GetJobStatusResponse_JOB_STATUS_QUEUED {
		log.Printf("[COORDINATOR]: Transitioning job %s to RUNNING state", task.JobID)
		job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_RUNNING)
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
		log.Printf("[ERROR]: Failed to get job %s for progress update: %v", task.JobID, err)
		return &pb.ReportTaskResultResponse{Acknowledged: false}, err
	}

	var complete bool
	switch jobType := job.(type) {
	case *RenderJob:
		jobType.mu.Lock()
		jobType.CompletedTasks++
		log.Printf("[COORDINATOR] Job %s progress: %d/%d tasks complete", task.JobID, jobType.CompletedTasks, jobType.TotalTasks)
		complete = jobType.CompletedTasks == jobType.TotalTasks
		jobType.mu.Unlock()

	case *CompositeJob:
		jobType.mu.Lock()
		jobType.CompletedFrames++
		log.Printf("[COORDINATOR] Job %s progress: %d/%d frames complete", task.JobID, jobType.CompletedFrames, jobType.TotalFrames)
		complete = jobType.CompletedFrames == jobType.TotalFrames
		jobType.mu.Unlock()
	}

	// Queue downstream dependencies if job is complete
	if complete {
		log.Printf("[COORDINATOR]: Job %s is 100%% complete! Marking as COMPLETED.", job.ID())
		job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_COMPLETED)

		// Ask the tracker to safely update the math and report unlocked dependencies
		unlockedJobs := s.tracker.UnlockDependencies(job.ID())
		log.Printf("[COORDINATOR]: Unlocked %d downstream jobs for job %s", len(unlockedJobs), job.ID())

		// Loop through whatever jobs just hit 0 dependencies and queue them
		for _, newJob := range unlockedJobs {
			log.Printf("[COORDINATOR]: Job %s is fully unlocked and ready to queue!", newJob.ID())

			// Type assert to figure out what kind of job just unlocked and queue it
			req := newJob.GetOriginalReq()
			var err error
			switch typedJob := newJob.(type) {
			case *RenderJob:
				err = s.handleRenderJobSubmit(typedJob.ID(), req, req.GetRenderJob())
			case *CompositeJob:
				err = s.handleCompositeJobSubmit(typedJob.ID(), req, req.GetCompositeJob())
			}
			if err != nil {
				log.Printf("[ERROR] Failed to queue unlocked job %s: %v", newJob.ID(), err)
			}
		}
	}

	return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
}

func (s *Server) ReportTaskProgress(ctx context.Context, req *pb.ReportTaskProgressRequest) (*pb.ReportTaskProgressResponse, error) {
	s.scheduler.UpdateHeartbeat(req.GetTaskId())
	return &pb.ReportTaskProgressResponse{Acknowledged: true}, nil
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

			// Determine global limit (default to 4 in local mode, 20 in cloud)
			limit := 20
			if s.localStorageBase != "" {
				limit = 4 // LOCAL MODE: Allow some parallelism by default
			}
			if val, err := strconv.Atoi(os.Getenv("MAX_WORKERS")); err == nil && val > 0 {
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

// relativePathEscapesRoot is true for cleaned relative paths that step outside
// their base directory (".." or "../..."). Segment names like "..foo" are allowed.
func relativePathEscapesRoot(clean string) bool {
	if clean == ".." {
		return true
	}
	return strings.HasPrefix(clean, ".."+string(filepath.Separator))
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
		return filepath.Join(s.localStorageBase, filepath.Clean(rel)), nil
	}

	// Repo-relative paths: workspace root is mounted at localStorageBase (/data).
	// We MUST join them to ensure we get /data/data/scenes/... etc.
	if !filepath.IsAbs(uri) {
		return filepath.Join(s.localStorageBase, filepath.Clean(uri)), nil
	}

	return uri, nil
}
func (s *Server) handleRenderJobSubmit(jobID string, req *pb.SubmitJobRequest, job *pb.RenderJob) error {
	outputUriPrefix, err := s.translateLocalPath(job.GetOutputUriPrefix())
	if err != nil {
		return err
	}
	sceneUri, err := s.translateLocalPath(job.GetSceneUri())
	if err != nil {
		return err
	}

	extension := ".exr"
	if !job.GetEnableDeep() {
		extension = ".png"
	}

	numFrames := int32(req.GetNumFrames())
	tasks := make([]*pb.RenderTask, 0, numFrames)

	for frameID := int32(0); frameID < numFrames; frameID++ {
		if frameID == 0 {
			log.Printf("[COORDINATOR] Scene path: raw=%q -> worker=%q", job.GetSceneUri(), sceneUri)
		}

		// Replace #### with padded frame ID if it exists in the scene URI
		frameSceneUri := strings.ReplaceAll(sceneUri, "####", fmt.Sprintf("%04d", frameID+1))

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
		tasks = append(tasks, task)
	}

	// Dispatch to scheduler asynchronously so the gRPC handler doesn't block if the queue is full
	go func(tasks []*pb.RenderTask) {
		for frameID, task := range tasks {
			if _, err := s.scheduler.EnqueueTask(task, jobID, fmt.Sprint(frameID)); err != nil {
				log.Printf("[ERROR]: Failed to enqueue render task for job %s frame %d (%v)", jobID, frameID, err)
			}
		}
	}(tasks)

	return nil
}

func (s *Server) handleCompositeJobSubmit(jobID string, req *pb.SubmitJobRequest, job *pb.CompositeJob) error {
	log.Printf("[COORDINATOR]: Handling composite job submit for job %s", jobID)

	outputUriPrefix, err := s.translateLocalPath(job.GetOutputUriPrefix())
	if err != nil {
		log.Printf("[ERROR]: Failed to translate output path for job %s: %v", jobID, err)
		return err
	}
	log.Printf("[COORDINATOR]: Output URI Prefix for job %s: %s", jobID, outputUriPrefix)

	// Resolve layers: prioritize explicit LayerUriPrefixes, fallback to DependsOn discovery
	layerPrefixes := job.GetLayerUriPrefixes()
	if len(layerPrefixes) == 0 {
		log.Printf("[COORDINATOR]: No layers provided for job %s. Discovering from dependencies: %v", jobID, req.DependsOn)
		for _, depID := range req.DependsOn {
			depJob, err := s.tracker.GetJob(depID)
			if err != nil {
				log.Printf("[ERROR]: Failed to resolve dependency %s for composite discovery: %v", depID, err)
				return fmt.Errorf("[ERROR]: Failed to resolve dependency %s for composite discovery: %w", depID, err)
			}
			prefix := depJob.GetOutputPrefix()
			if prefix != "" {
				// Resolve extension
				ext := ".exr"
				if rj, ok := depJob.(*RenderJob); ok && !rj.OriginalReq.GetRenderJob().GetEnableDeep() {
					ext = ".png"
				}
				numLayerFrames := depJob.GetOriginalReq().GetNumFrames()
				log.Printf("[COORDINATOR]: Discovered layer prefix from job %s: %s (Ext: %s, Frames: %d)", depID, prefix, ext, numLayerFrames)
				layerPrefixes = append(layerPrefixes, fmt.Sprintf("%s|%s|%d", prefix, ext, numLayerFrames))
			}
		}
	}

	if len(layerPrefixes) == 0 {
		log.Printf("[ERROR]: Composite job %s has no input layers and no resolvable dependencies", jobID)
		return fmt.Errorf("[ERROR]: Composite job %s has no input layers and no resolvable dependencies", jobID)
	}

	// Use inherited/provided dimensions
	width := req.GetWidth()
	height := req.GetHeight()
	log.Printf("[COORDINATOR]: Composite job %s dimensions: %dx%d", jobID, width, height)

	numFrames := int(req.GetNumFrames())
	log.Printf("[COORDINATOR]: Enqueuing %d composite tasks for job %s", numFrames, jobID)

	// Pre-resolve prefixes outside of the massive frame loop to prevent redundant allocations and GetEnv calls
	type LayerDef struct {
		Prefix    string
		Ext       string
		NumFrames int
	}
	translatedPrefixes := make([]LayerDef, len(layerPrefixes))

	for idx, entry := range layerPrefixes {
		parts := strings.Split(entry, "|")
		uriPrefix := parts[0]
		extension := ".exr"
		numLayerFrames := 1
		if len(parts) > 1 {
			extension = parts[1]
		}
		if len(parts) > 2 {
			if parsed, err := strconv.Atoi(parts[2]); err == nil {
				numLayerFrames = parsed
			}
		}
		translatedPrefix, err := s.translateLocalPath(uriPrefix)
		if err != nil {
			return err
		}
		translatedPrefixes[idx] = LayerDef{Prefix: translatedPrefix, Ext: extension, NumFrames: numLayerFrames}
	}

	tasks := make([]*pb.CompositeTask, 0, numFrames)

	for frameID := 0; frameID < numFrames; frameID++ {

		// Create a fresh slice for THIS specific frame task
		frameLayerUris := make([]string, len(translatedPrefixes))

		for idx, p := range translatedPrefixes {
			layerTargetFrame := frameID
			if p.NumFrames == 1 {
				layerTargetFrame = 0
			} else if layerTargetFrame >= p.NumFrames {
				layerTargetFrame = p.NumFrames - 1
			}

			fullLayerUri, err := url.JoinPath(p.Prefix, fmt.Sprintf("frame-%04d%s", layerTargetFrame+1, p.Ext))
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

			Width:  width,
			Height: height,

			OutputUri: finalOutputUri,
		}
		tasks = append(tasks, task)
	}

	// Dispatch to scheduler asynchronously so the gRPC handler doesn't block if the queue is full
	go func(tasks []*pb.CompositeTask) {
		for frameID, task := range tasks {
			if _, err := s.scheduler.EnqueueTask(task, jobID, fmt.Sprint(frameID)); err != nil {
				log.Printf("[ERROR]: Failed to enqueue composite task for job %s frame %d (%v)", jobID, frameID, err)
			}
		}
	}(tasks)

	return nil
}

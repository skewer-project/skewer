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
		// scheduler.GetNextTask accepts `capabilities` so it only
		// hands Loom tasks to Loom workers, and Skewer tasks to Skewer workers.
		task, err := s.scheduler.GetNextTask(stream.Context(), capabilities)
		if err != nil {
			// If the context is cancelled (worker disconnected), exit cleanly
			log.Printf("[SERVER]: Worker %s stream closed: %v", workerID, err)
			return err
		}

		// Lazy cancellation check
		job, err := s.tracker.GetJob(task.JobID)

		// If the job is marked as FAILED, throw this task in the trash!
		if err == nil && job.GetStatus() == pb.GetJobStatusResponse_JOB_STATUS_FAILED {
			log.Printf("[SERVER]: Skipping cancelled task %s for job %s", task.ID, task.JobID)
			s.scheduler.MarkTaskComplete(task.ID) // Remove from active memory
			continue                              // Loop back to the top and grab the next task
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
		case *pb.MergeTask:
			workPackage.Payload = &pb.WorkPackage_MergeTask{MergeTask: t}
		case *pb.CompositeTask:
			workPackage.Payload = &pb.WorkPackage_CompositeTask{CompositeTask: t}
		default:
			log.Printf("[ERROR]: Unknown task payload type for task %s", task.ID)
			continue
		}

		// Send workPackage down the wire
		if err := stream.Send(workPackage); err != nil {
			log.Printf("[ERROR]: Failed to send task to worker %s: %v", workerID, err)
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

	log.Printf("[SERVER] Task %s completed by worker %s: success=%v", taskID, req.WorkerId, req.Success)

	// Tell the scheduler the worker is officially done with it (stops the Sweeper timeout)
	task, exists := s.scheduler.MarkTaskComplete(taskID)

	if !req.Success {
		// Handle failure. Requeue the task, or increment a failure counter
		// in the JobTracker and fail the whole job if it exceeds max retries.
		if exists {
			task.Retries++
			if task.Retries > 3 {
				log.Printf("[SERVER] Task %s failed too many times. Failing Job %s", taskID, jobID)
				s.tracker.activeJobs[jobID].SetStatus(pb.GetJobStatusResponse_JOB_STATUS_FAILED)
				s.scheduler.PurgeJobTasks(jobID) // Stop other tasks for this job
			} else {
				s.scheduler.RequeueTask(taskID)
			}
		} else {
			// fmt.Errorf("[ERROR]: Task %s not found in active tasks", taskID)
			return &pb.ReportTaskResultResponse{Acknowledged: false}, nil
		}
		return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
	}

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

		// Update the specific frame's progress
		frameState := jobType.Frames[task.FrameID]
		frameState.CompletedChunks++

		// Check if frame is complete
		if frameState.CompletedChunks == frameState.TotalChunks {
			log.Printf("Frame %s for job %s is fully rendered! Queuing MergeTask.", task.FrameID, jobID)

			// NOW we hand it to the scheduler
			s.scheduler.EnqueueTask(frameState.PendingMerge, jobID, task.FrameID)

			// Free up memory
			frameState.PendingMerge = nil
		}
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
			// (You will eventually call s.handleCompositeJobSubmit here
			// to actually queue the tasks for the newly unlocked job)
			log.Printf("Job %s is fully unlocked and ready to queue!", newJob.ID())

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

func (s *Server) handleRenderJobSubmit(jobID string, req *pb.SubmitJobRequest, job *pb.RenderJob) error {
	numSamplesPerWorker := job.GetTotalSamples() / job.GetSampleDivision()
	extraSamplesPerWorker := job.GetTotalSamples() % job.GetSampleDivision()

	for frameID := range req.NumFrames {
		var sampleStart int32 = 0
		var outputUris []string
		for chunk := range numSamplesPerWorker {
			uriSuffix := fmt.Sprintf("frame-%d-chunk-%d", frameID, chunk)

			// Add in remainder
			var sampleEnd int32 = sampleStart + numSamplesPerWorker
			if extraSamplesPerWorker > 0 {
				extraSamplesPerWorker--
				sampleEnd++
			}

			// Safe URI joining with net/url
			outputUri, err := url.JoinPath(job.GetOutputUriPrefix(), uriSuffix)
			if err != nil {
				return err
			}

			outputUris = append(outputUris, outputUri)

			task := &pb.RenderTask{
				SceneUri: job.GetSceneUri(),
				Width:    req.GetWidth(),
				Height:   req.GetHeight(),

				SampleStart: sampleStart,
				SampleEnd:   sampleEnd, // End is EXCLUSIVE

				OutputUri: outputUri,
			}
			s.scheduler.EnqueueTask(task, jobID, fmt.Sprint(frameID))

			sampleStart = sampleEnd
		}

		// Create the Merge task
		mergeUri, err := url.JoinPath(job.GetOutputUriPrefix(), fmt.Sprintf("frame-%d", frameID))
		if err != nil {
			return err
		}

		mergeTask := &pb.MergeTask{
			PartialDeepExrUris: outputUris,
			OutputUri:          mergeUri,
		}

		// DON'T enqueue MergeTask. Store it in memory instead and wait until all chunks are completed
		genericJob, err := s.tracker.GetJob(req.GetJobId())
		if err != nil {
			return err
		}
		renderJob := genericJob.(*RenderJob)

		// While this is technically safe because the tasks haven't been queued yet, it's probably better
		// to lock since we're modifying the maps, just in case another goroutine happens to query GetJobStatus
		// at that exact millisecond
		renderJob.mu.Lock()
		if renderJob.Frames == nil {
			renderJob.Frames = make(map[string]*FrameState)
		}

		renderJob.Frames[fmt.Sprint(frameID)] = &FrameState{
			CompletedChunks: 0,
			TotalChunks:     renderJob.SampleDivision,
			PendingMerge:    mergeTask,
		}
		renderJob.mu.Unlock()
	}

	return nil
}

func (s *Server) handleCompositeJobSubmit(jobID string, req *pb.SubmitJobRequest, job *pb.CompositeJob) error {

	for frameID := range req.NumFrames {

		// gs://bucket/renders/smoke
		layerUris := job.GetLayerUriPrefixes()
		for idx, uriPrefix := range layerUris {
			fullLayerUri, err := url.JoinPath(uriPrefix, fmt.Sprintf("frame-%d", frameID))
			if err != nil {
				return err
			}

			layerUris[idx] = fullLayerUri
		}

		finalOutputUri, err := url.JoinPath(job.GetOutputUriPrefix(), fmt.Sprintf("frame-%d", frameID))
		if err != nil {
			return err
		}

		task := &pb.CompositeTask{
			LayerUris: layerUris,

			Width:  req.GetWidth(),
			Height: req.GetHeight(),

			OutputUri: finalOutputUri,
		}
		s.scheduler.EnqueueTask(task, jobID, fmt.Sprint(frameID))

	}

	return nil
}

// func generateJobID() string {
// 	return "job-" + time.Now().Format("20060102150405")
// }

package coordinator

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"log/slog"
	"net/url"
	"strings"

	"github.com/google/uuid"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

// Server implements the gRPC CoordinatorService
type Server struct {
	pb.UnimplementedCoordinatorServiceServer
	scheduler *Scheduler
	tracker   *JobTracker
	cloud     CloudManager
}

func NewServer(scheduler *Scheduler, tracker *JobTracker, cloud CloudManager) *Server {
	s := &Server{
		scheduler: scheduler,
		tracker:   tracker,
		cloud:     cloud,
	}

	s.scheduler.OnTaskFailedPermanently = func(task *Task) {
		slog.Warn("task failed permanently, failing job", "task_id", task.ID, "job_id", task.JobID)
		s.tracker.CancelJob(task.JobID)
		_ = s.scheduler.PurgeJobTasks(context.Background(), task.JobID)
	}

	return s
}

// Stop exists for API compatibility; push-based dispatch has no background capacity loop.
func (s *Server) Stop() {}

// ================= //
// * EXTERNAL API *  //
// ================= //

func (s *Server) SubmitJob(ctx context.Context, req *pb.SubmitJobRequest) (*pb.SubmitJobResponse, error) {
	jobID := req.JobId
	if jobID == "" {
		jobID = uuid.New().String()
	}

	slog.Info("received SubmitJob", "job_id", jobID)

	var newJob Job

	switch req.JobType.(type) {
	case *pb.SubmitJobRequest_RenderJob:
		newJob = &RenderJob{
			JobID:        jobID,
			Dependencies: req.DependsOn,
			Status:       pb.GetJobStatusResponse_JOB_STATUS_UNSPECIFIED,
			TotalTasks:   req.NumFrames,
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

	if err := s.tracker.AddJob(newJob); err != nil {
		return nil, err
	}

	// Dispatch immediately if there are no dependencies, OR if all dependencies
	// were already complete when this job was added (AddJob pre-decrements in that case).
	if newJob.GetStatus() == pb.GetJobStatusResponse_JOB_STATUS_QUEUED {
		var err error
		switch req.JobType.(type) {
		case *pb.SubmitJobRequest_RenderJob:
			err = s.handleRenderJobSubmit(ctx, jobID, req, req.GetRenderJob())
		case *pb.SubmitJobRequest_CompositeJob:
			err = s.handleCompositeJobSubmit(ctx, jobID, req, req.GetCompositeJob())
		}

		if err != nil {
			return nil, err
		}
	}

	return &pb.SubmitJobResponse{JobId: jobID}, nil
}

func (s *Server) GetJobStatus(ctx context.Context, req *pb.GetJobStatusRequest) (*pb.GetJobStatusResponse, error) {
	job, err := s.tracker.GetJob(req.JobId)
	if err != nil {
		return &pb.GetJobStatusResponse{
			JobStatus:    pb.GetJobStatusResponse_JOB_STATUS_FAILED,
			ErrorMessage: fmt.Sprintf("Job not found: %v", err),
		}, nil
	}

	return &pb.GetJobStatusResponse{
		JobStatus:       job.GetStatus(),
		ProgressPercent: job.Progress() * 100,
	}, nil
}

func (s *Server) CancelJob(ctx context.Context, req *pb.CancelJobRequest) (*pb.CancelJobResponse, error) {
	err := s.tracker.CancelJob(req.JobId)

	if err != nil {
		return &pb.CancelJobResponse{
			Success: false,
		}, nil
	}

	_ = s.scheduler.PurgeJobTasks(ctx, req.JobId)

	return &pb.CancelJobResponse{
		Success: true,
	}, nil
}

func (s *Server) ReportTaskResult(ctx context.Context, req *pb.ReportTaskResultRequest) (*pb.ReportTaskResultResponse, error) {
	taskID := req.GetTaskId()
	jobID := req.GetJobId()

	slog.Info("task result received", "task_id", taskID, "worker_id", req.WorkerId, "success", req.Success)

	task, exists := s.scheduler.Task(taskID)
	if !exists {
		slog.Error("task not found in active tasks", "task_id", taskID)
		return &pb.ReportTaskResultResponse{Acknowledged: false}, fmt.Errorf("[ERROR]: Task %s not found in active tasks", taskID)
	}

	if !req.Success {
		task.Retries++
		if task.Retries > 3 {
			slog.Warn("task exceeded max retries, failing job", "task_id", taskID, "job_id", jobID)

			err := s.tracker.CancelJob(jobID)
			if err != nil {
				slog.Error("failed to cancel job after task failure", "job_id", jobID, "error", err)
				return &pb.ReportTaskResultResponse{Acknowledged: false}, err
			}

			_ = s.scheduler.PurgeJobTasks(ctx, jobID)
		} else {
			s.scheduler.RequeueTask(ctx, taskID)
		}

		return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
	}

	s.scheduler.MarkTaskComplete(taskID)

	// Write cache sentinel so future identical tasks are skipped.
	if task.SentinelURI != "" && s.cloud != nil {
		if werr := s.cloud.WriteSentinel(ctx, task.SentinelURI); werr != nil {
			slog.Warn("failed to write cache sentinel", "uri", task.SentinelURI, "error", werr)
		} else {
			slog.Debug("cache sentinel written", "uri", task.SentinelURI)
		}
	}

	job, err := s.tracker.GetJob(task.JobID)
	if err != nil {
		return &pb.ReportTaskResultResponse{Acknowledged: false}, err
	}

	var complete bool
	switch jobType := job.(type) {
	case *RenderJob:
		jobType.mu.Lock()
		jobType.CompletedTasks++
		complete = jobType.CompletedTasks == jobType.TotalTasks
		jobType.mu.Unlock()

	case *CompositeJob:
		jobType.mu.Lock()
		jobType.CompletedFrames++
		complete = jobType.CompletedFrames == jobType.TotalFrames
		jobType.mu.Unlock()
	}

	if complete {
		job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_COMPLETED)

		unlockedJobs := s.tracker.UnlockDependencies(job.ID())

		for _, newJob := range unlockedJobs {
			slog.Info("job unlocked and queuing", "job_id", newJob.ID())

			orig := newJob.GetOriginalReq()
			switch typedJob := newJob.(type) {
			case *RenderJob:
				_ = s.handleRenderJobSubmit(ctx, typedJob.ID(), orig, orig.GetRenderJob())
			case *CompositeJob:
				_ = s.handleCompositeJobSubmit(ctx, typedJob.ID(), orig, orig.GetCompositeJob())
			}
		}
	}

	return &pb.ReportTaskResultResponse{Acknowledged: true}, nil
}

func gcsURIToFusePath(uri string) (string, error) {
	if !strings.HasPrefix(uri, "gs://") {
		return "", fmt.Errorf("cloud mode requires gs:// URIs, got: %s", uri)
	}
	return "/gcs/" + strings.TrimPrefix(uri, "gs://"), nil
}

func (s *Server) handleRenderJobSubmit(ctx context.Context, jobID string, req *pb.SubmitJobRequest, job *pb.RenderJob) error {
	rj, err := s.tracker.GetJob(jobID)
	if err != nil {
		return fmt.Errorf("job %s not found in tracker: %w", jobID, err)
	}
	renderJob, ok := rj.(*RenderJob)
	if !ok {
		return fmt.Errorf("job %s is not a RenderJob", jobID)
	}

	skippedTasks := int32(0)

	for frameID := int32(0); frameID < req.GetNumFrames(); frameID++ {
		outputUriPrefix, err := gcsURIToFusePath(job.GetOutputUriPrefix())
		if err != nil {
			return err
		}
		sceneUri, err := gcsURIToFusePath(job.GetSceneUri())
		if err != nil {
			return err
		}
		if frameID == 0 {
			slog.Debug("resolved scene path", "raw", job.GetSceneUri(), "fuse", sceneUri)
		}

		frameSceneUri := strings.ReplaceAll(sceneUri, "####", fmt.Sprintf("%04d", frameID+1))
		// GCS URI for this specific frame (#### expanded) — needed for cache key hashing.
		frameSceneGCSURI := strings.ReplaceAll(job.GetSceneUri(), "####", fmt.Sprintf("%04d", frameID+1))

		extension := ".exr"
		if !job.GetEnableDeep() {
			extension = ".png"
		}
		outputUri, err := url.JoinPath(outputUriPrefix, fmt.Sprintf("frame-%04d%s", frameID+1, extension))
		if err != nil {
			return err
		}

		// Cache check: compute a sentinel key from (scene GCS MD5 + layer_index + render params).
		sentinelURI, cacheKey, err := s.computeSentinel(ctx, frameSceneGCSURI, job, req)
		if err != nil {
			slog.Warn("cache check failed, proceeding without cache", "error", err)
		} else if sentinelURI != "" {
			exists, err := s.cloud.GCSObjectExists(ctx, sentinelURI)
			if err != nil {
				slog.Warn("sentinel existence check failed", "uri", sentinelURI, "error", err)
			} else if exists {
				slog.Info("cache hit, skipping render task", "job_id", jobID, "layer_name", job.GetLayerName(), "layer_index", job.GetLayerIndex())
				skippedTasks++
				continue
			}
		}

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
			LayerIndex:     job.GetLayerIndex(),
		}

		t, err := s.scheduler.EnqueueTaskWithMeta(ctx, task, jobID, fmt.Sprint(frameID), cacheKey, sentinelURI)
		if err != nil {
			return fmt.Errorf("[ERROR]: Failed to enqueue render task for job %s frame %d (%w)", jobID, frameID, err)
		}
		_ = t
	}

	// Adjust TotalTasks for partial cache hits so the completion check still fires
	// when the remaining (non-cached) tasks report back.
	if skippedTasks > 0 && skippedTasks < req.GetNumFrames() {
		renderJob.mu.Lock()
		renderJob.TotalTasks -= skippedTasks
		renderJob.mu.Unlock()
		slog.Info("partial cache hit", "job_id", jobID, "skipped", skippedTasks, "remaining", req.GetNumFrames()-skippedTasks)
	}

	// If all tasks were cache hits, mark the job complete immediately.
	if skippedTasks == req.GetNumFrames() {
		renderJob.mu.Lock()
		renderJob.CompletedTasks = renderJob.TotalTasks
		renderJob.mu.Unlock()
		renderJob.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_COMPLETED)
		slog.Info("all tasks cache hits, job complete", "job_id", jobID)

		unlockedJobs := s.tracker.UnlockDependencies(jobID)
		for _, newJob := range unlockedJobs {
			orig := newJob.GetOriginalReq()
			switch typedJob := newJob.(type) {
			case *RenderJob:
				_ = s.handleRenderJobSubmit(ctx, typedJob.ID(), orig, orig.GetRenderJob())
			case *CompositeJob:
				_ = s.handleCompositeJobSubmit(ctx, typedJob.ID(), orig, orig.GetCompositeJob())
			}
		}
	}

	return nil
}

// computeSentinel derives a cache sentinel GCS URI and its hash key for a render task.
// Returns ("", "", nil) if the scene URI is not a GCS path (local mode, no caching).
func (s *Server) computeSentinel(ctx context.Context, sceneGCSURI string, job *pb.RenderJob, req *pb.SubmitJobRequest) (sentinelURI, cacheKey string, err error) {
	if !strings.HasPrefix(sceneGCSURI, "gs://") || s.cloud == nil {
		return "", "", nil
	}
	fileHash, err := s.cloud.GCSObjectHash(ctx, sceneGCSURI)
	if err != nil {
		return "", "", err
	}
	// Mix in render params so changing resolution/samples invalidates the cache.
	raw := fmt.Sprintf("%s|%d|%d|%d|%d", fileHash, job.GetLayerIndex(), req.GetWidth(), req.GetHeight(), job.GetMaxSamples())
	sum := sha256.Sum256([]byte(raw))
	cacheKey = hex.EncodeToString(sum[:])

	// Derive bucket from the output_uri_prefix to keep sentinels colocated.
	bucket, _, err := parseGCSURI(job.GetOutputUriPrefix())
	if err != nil {
		return "", "", err
	}
	sentinelURI = fmt.Sprintf("gs://%s/renders/.cache/%s", bucket, cacheKey)
	return sentinelURI, cacheKey, nil
}

func (s *Server) handleCompositeJobSubmit(ctx context.Context, jobID string, req *pb.SubmitJobRequest, job *pb.CompositeJob) error {

	outputUriPrefix, err := gcsURIToFusePath(job.GetOutputUriPrefix())
	if err != nil {
		return err
	}

	for frameID := 0; frameID < int(req.GetNumFrames()); frameID++ {

		originalPrefixes := job.GetLayerUriPrefixes()

		frameLayerUris := make([]string, len(originalPrefixes))

		staticSet := make(map[int32]bool)
		for _, si := range job.GetStaticLayerIndices() {
			staticSet[si] = true
		}

		for idx, uriPrefix := range originalPrefixes {
			translatedPrefix, err := gcsURIToFusePath(uriPrefix)
			if err != nil {
				return err
			}

			frameNum := frameID + 1
			if staticSet[int32(idx)] {
				frameNum = 1 // static layer: always use frame-0001.exr
			}

			fullLayerUri, err := url.JoinPath(translatedPrefix, fmt.Sprintf("frame-%04d.exr", frameNum))
			if err != nil {
				return err
			}
			frameLayerUris[idx] = fullLayerUri
		}

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
		if _, err := s.scheduler.EnqueueTask(ctx, task, jobID, fmt.Sprint(frameID)); err != nil {
			return fmt.Errorf("[ERROR]: Failed to enqueue composite task for job %s frame %d: %w", jobID, frameID, err)
		}

	}

	return nil
}

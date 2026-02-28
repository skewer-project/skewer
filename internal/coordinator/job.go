package coordinator

import (
	"fmt"
	"sync"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

// ================= //
// * Job Primitive * //
// ================= //

// Minimum for Job interface
type Job interface {
	ID() string
	GetDependencies() []string
	GetStatus() pb.GetJobStatusResponse_JobStatus
	SetStatus(status pb.GetJobStatusResponse_JobStatus)
	Progress() float32
	GetOriginalReq() *pb.SubmitJobRequest
}

type FrameState struct {
	CompletedChunks int32
	TotalChunks     int32
	PendingMerge    *pb.MergeTask // Hold the task here until it's ready
}

// The Render Job
type RenderJob struct {
	JobID        string
	Dependencies []string
	Status       pb.GetJobStatusResponse_JobStatus

	// Render-specific fields
	CompletedTasks int32
	TotalTasks     int32
	SampleDivision int32

	Frames map[string]*FrameState
	mu     sync.Mutex // Protects the map during concurrent worker updates

	OriginalReq *pb.SubmitJobRequest
}

func (rj *RenderJob) ID() string                                   { return rj.JobID } // Fulfills the DAGNode interface as well
func (rj *RenderJob) GetDependencies() []string                    { return rj.Dependencies }
func (rj *RenderJob) GetStatus() pb.GetJobStatusResponse_JobStatus { return rj.Status }
func (rj *RenderJob) SetStatus(status pb.GetJobStatusResponse_JobStatus) {
	rj.mu.Lock()
	defer rj.mu.Unlock()

	rj.Status = status
}
func (rj *RenderJob) Progress() float32 {
	rj.mu.Lock()
	defer rj.mu.Unlock()

	if rj.TotalTasks == 0 {
		return 0
	}
	return float32(rj.CompletedTasks) / float32(rj.TotalTasks)
}
func (rj *RenderJob) GetOriginalReq() *pb.SubmitJobRequest {
	return rj.OriginalReq
}

// The Composite Job
type CompositeJob struct {
	JobID        string
	Dependencies []string
	Status       pb.GetJobStatusResponse_JobStatus

	// Composite-specific fields
	CompletedFrames int32
	TotalFrames     int32

	mu sync.Mutex

	OriginalReq *pb.SubmitJobRequest
}

func (cj *CompositeJob) ID() string                                   { return cj.JobID } // Fulfills the DAGNode interface as well
func (cj *CompositeJob) GetDependencies() []string                    { return cj.Dependencies }
func (cj *CompositeJob) GetStatus() pb.GetJobStatusResponse_JobStatus { return cj.Status }
func (cj *CompositeJob) SetStatus(status pb.GetJobStatusResponse_JobStatus) {
	cj.mu.Lock()
	defer cj.mu.Unlock()

	cj.Status = status
}
func (cj *CompositeJob) Progress() float32 {
	cj.mu.Lock()
	defer cj.mu.Unlock()

	if cj.TotalFrames == 0 {
		return 0
	}
	return float32(cj.CompletedFrames) / float32(cj.TotalFrames)
}
func (rj *CompositeJob) GetOriginalReq() *pb.SubmitJobRequest {
	return rj.OriginalReq
}

// ===================== //
// * Job Tracker Logic * //
// ===================== //

// A bit of denormalization for state recovery
type JobTracker struct {
	mu sync.RWMutex

	graph DAG // Unchanging DAG for retry and recovery

	// The live countdowns (Dynamic: Decrements as workers report success)
	pendingDeps map[string]int

	// Quick memory access to the actual job payloads
	activeJobs map[string]Job
}

func (jt *JobTracker) AddJob(job Job) error {
	// Check if job already exists
	jt.mu.Lock()
	defer jt.mu.Unlock()

	if _, exists := jt.activeJobs[job.ID()]; exists {
		return fmt.Errorf("[ERROR] Adding job that already exists with ID %s.", job.ID())
	} else {
		jobDeps := job.GetDependencies()

		jt.activeJobs[job.ID()] = job // ALWAYS add it to active jobs!

		// Add the job to the live tracker
		if len(jobDeps) == 0 {
			job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_QUEUED)
		} else {
			job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_PENDING_DEPENDENCIES)
		}
		jt.pendingDeps[job.ID()] = len(jobDeps)

		// Add the job and dependencies to the graph
		jt.graph.AddNode(job)
		for _, depID := range job.GetDependencies() {
			// Get the dependency node from the graph (if it exists)
			dep, err := jt.graph.GetNode(depID)
			if err != nil {
				return fmt.Errorf("[ERROR] Job with ID %s has dependencies that don't exist. Please add dependent jobs before.", job.ID())
			}
			jt.graph.AddDependency(job, dep)
		}

		return nil
	}
}

func (jt *JobTracker) GetJob(jobID string) (Job, error) {
	jt.mu.RLock() // Read lock is a bit faster
	defer jt.mu.RUnlock()

	job, exists := jt.activeJobs[jobID]
	if !exists {
		return nil, fmt.Errorf("[ERROR] Job with ID %s not found.", jobID)
	}
	return job, nil
}

func (jt *JobTracker) CancelJob(jobID string) error {
	jt.mu.Lock()
	defer jt.mu.Unlock()

	job, exists := jt.activeJobs[jobID]
	if !exists {
		return fmt.Errorf("[ERROR] Job with ID %s not found.", jobID)
	}

	// Mark job as FAILED for now in JobTracker (maybe we can add JOB_STATUS_CANCELLED)
	job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_FAILED)

	// Flush any pending tasks for this job from the Scheduler queue
	return nil
}

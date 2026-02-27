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
}

func (rj *RenderJob) ID() string                                   { return rj.JobID } // Fulfills the DAGNode interface as well
func (rj *RenderJob) GetDependencies() []string                    { return rj.Dependencies }
func (rj *RenderJob) GetStatus() pb.GetJobStatusResponse_JobStatus { return rj.Status }
func (rj *RenderJob) SetStatus(status pb.GetJobStatusResponse_JobStatus) {
	rj.Status = status
}
func (rj *RenderJob) Progress() float32 {
	if rj.TotalTasks == 0 {
		return 0
	}
	return float32(rj.CompletedTasks) / float32(rj.TotalTasks)
}

// The Composite Job
type CompositeJob struct {
	JobID        string
	Dependencies []string
	Status       pb.GetJobStatusResponse_JobStatus

	// Composite-specific fields
	CompletedFrames int32
	TotalFrames     int32
}

func (cj *CompositeJob) ID() string                                   { return cj.JobID } // Fulfills the DAGNode interface as well
func (cj *CompositeJob) GetDependencies() []string                    { return cj.Dependencies }
func (cj *CompositeJob) GetStatus() pb.GetJobStatusResponse_JobStatus { return cj.Status }
func (cj *CompositeJob) SetStatus(status pb.GetJobStatusResponse_JobStatus) {
	cj.Status = status
}
func (cj *CompositeJob) Progress() float32 {
	if cj.TotalFrames == 0 {
		return 0
	}
	return float32(cj.CompletedFrames) / float32(cj.TotalFrames)
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
	if _, exists := jt.activeJobs[job.ID()]; exists {
		return fmt.Errorf("[ERROR] Adding job that already exists with ID %s.", job.ID())
	} else {
		jobDeps := job.GetDependencies()

		// Add the job to the live tracker
		if len(jobDeps) == 0 {
			job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_PENDING_DEPENDENCIES)
		} else {
			job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_QUEUED)
			jt.activeJobs[job.ID()] = job
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
	job, exists := jt.activeJobs[jobID]
	if !exists {
		return nil, fmt.Errorf("[ERROR] Job with ID %s not found.", jobID)
	}
	return job, nil
}

func (jt *JobTracker) CancelJob(jobID string) error {
	job, exists := jt.activeJobs[jobID]
	if !exists {
		return fmt.Errorf("[ERROR] Job with ID %s not found.", jobID)
	}

	// Mark job as FAILED for now in JobTracker (maybe we can add JOB_STATUS_CANCELLED)
	job.SetStatus(pb.GetJobStatusResponse_JOB_STATUS_FAILED)

	// Flush any pending tasks for this job from the Scheduler queue
	return nil
}

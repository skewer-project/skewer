package coordinator

import (
	"context"
	"log/slog"
	"sync"
	"time"

	"github.com/google/uuid"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

type Task struct {
	ID      string
	JobID   string
	FrameID string
	Payload any // *pb.RenderTask or *pb.CompositeTask

	CacheKey    string // SHA-256 hex used as sentinel path suffix; empty if caching not applicable
	SentinelURI string // gs:// path to write on successful completion

	CreatedAt time.Time
	StartedAt time.Time
	Retries   int32
}

type Scheduler struct {
	mu          sync.Mutex
	manager     CloudManager
	activeTasks map[string]*Task

	coordinatorAddr string

	OnTaskFailedPermanently func(task *Task)
}

func NewScheduler(manager CloudManager, coordinatorAddr string) *Scheduler {
	return &Scheduler{
		manager:         manager,
		activeTasks:     make(map[string]*Task),
		coordinatorAddr: coordinatorAddr,
	}
}

// EnqueueTask registers the task and launches a Cloud Run Job execution.
func (s *Scheduler) EnqueueTask(ctx context.Context, payload any, jobID string, frameID string) (string, error) {
	return s.EnqueueTaskWithMeta(ctx, payload, jobID, frameID, "", "")
}

// EnqueueTaskWithMeta is like EnqueueTask but attaches a cache key and sentinel URI to the task.
// On successful completion the caller is responsible for writing the sentinel via CloudManager.
func (s *Scheduler) EnqueueTaskWithMeta(ctx context.Context, payload any, jobID string, frameID string, cacheKey string, sentinelURI string) (string, error) {
	taskID := uuid.New().String()
	task := &Task{
		ID:          taskID,
		JobID:       jobID,
		FrameID:     frameID,
		Payload:     payload,
		CacheKey:    cacheKey,
		SentinelURI: sentinelURI,
		CreatedAt:   time.Now(),
	}

	workerType, env, err := buildWorkerLaunch(task, s.coordinatorAddr)
	if err != nil {
		return "", err
	}

	s.mu.Lock()
	task.StartedAt = time.Now()
	s.activeTasks[task.ID] = task
	s.mu.Unlock()

	if err := s.manager.LaunchTask(ctx, workerType, taskID, env); err != nil {
		s.popActiveTask(taskID)
		return "", err
	}

	return taskID, nil
}

func (s *Scheduler) Task(taskID string) (*Task, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	task, exists := s.activeTasks[taskID]
	return task, exists
}

func (s *Scheduler) popActiveTask(taskID string) (*Task, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	task, exists := s.activeTasks[taskID]
	if exists {
		delete(s.activeTasks, taskID)
	}
	return task, exists
}

func (s *Scheduler) RequeueTask(ctx context.Context, taskID string) {
	task, exists := s.popActiveTask(taskID)
	if !exists {
		return
	}
	task.CreatedAt = time.Now()
	task.Retries++

	if task.Retries > 3 {
		slog.Warn("task dropped permanently", "task_id", task.ID, "retries", task.Retries)
		if s.OnTaskFailedPermanently != nil {
			s.OnTaskFailedPermanently(task)
		}
		return
	}

	workerType, env, err := buildWorkerLaunch(task, s.coordinatorAddr)
	if err != nil {
		slog.Error("requeue build env failed", "task_id", task.ID, "error", err)
		if s.OnTaskFailedPermanently != nil {
			s.OnTaskFailedPermanently(task)
		}
		return
	}

	s.mu.Lock()
	task.StartedAt = time.Now()
	s.activeTasks[task.ID] = task
	s.mu.Unlock()

	if err := s.manager.LaunchTask(ctx, workerType, taskID, env); err != nil {
		slog.Error("requeue launch failed", "task_id", task.ID, "error", err)
		s.popActiveTask(taskID)
		if s.OnTaskFailedPermanently != nil {
			s.OnTaskFailedPermanently(task)
		}
	}
}

// MarkTaskComplete removes it from active tracking.
func (s *Scheduler) MarkTaskComplete(taskID string) (*Task, bool) {
	return s.popActiveTask(taskID)
}

// GetQueueLength is always zero (push-based dispatch).
func (s *Scheduler) GetQueueLength() int {
	return 0
}

func (s *Scheduler) GetQueueLengths() map[string]int {
	return map[string]int{
		"skewer": 0,
		"loom":   0,
	}
}

func (s *Scheduler) GetActiveTaskCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.activeTasks)
}

func (s *Scheduler) GetActiveTaskCounts() map[string]int {
	s.mu.Lock()
	defer s.mu.Unlock()

	counts := map[string]int{
		"skewer": 0,
		"loom":   0,
	}

	for _, task := range s.activeTasks {
		switch task.Payload.(type) {
		case *pb.RenderTask:
			counts["skewer"]++
		case *pb.CompositeTask:
			counts["loom"]++
		}
	}

	return counts
}

func (s *Scheduler) PurgeJobTasks(ctx context.Context, jobID string) error {
	s.mu.Lock()
	var ids []string
	for id, task := range s.activeTasks {
		if task.JobID == jobID {
			ids = append(ids, id)
		}
	}
	s.mu.Unlock()

	for _, taskID := range ids {
		if err := s.manager.CancelTask(ctx, taskID); err != nil {
			slog.Warn("cancel cloud execution", "task_id", taskID, "error", err)
		}
		s.popActiveTask(taskID)
	}

	return nil
}

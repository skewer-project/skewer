package coordinator

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/google/uuid"
)

type Job struct {
	ID        string
	JobName   string
	DependsOn []string
	NumFrames int32
	Width     int32
	Height    int32
	JobType   interface{} // *pb.RenderTask or *pb.CompositeTask
	CreatedAt time.Time
}

type Task struct {
	ID        string
	JobID     string
	FrameID   string
	Payload   interface{} // *pb.RenderTask or *pb.CompositeTask
	CreatedAt time.Time
}

type Scheduler struct {
	mu          sync.Mutex
	taskQueue   chan *Task       // Pending tasks (thread safe)
	activeTasks map[string]*Task // Tasks currently being worked on
}

func NewScheduler(maxQueueSize int) *Scheduler {
	return &Scheduler{
		taskQueue:   make(chan *Task, maxQueueSize),
		activeTasks: make(map[string]*Task),
	}
}

// EnqueueTask adds a new task to the queue
func (s *Scheduler) EnqueueTask(payload interface{}, jobID string, frameID string) (string, error) {
	taskID := uuid.New().String()
	task := &Task{
		ID:        taskID,
		JobID:     jobID,
		FrameID:   frameID,
		Payload:   payload,
		CreatedAt: time.Now(),
	}

	// Adding to the queue here must be atomic
	select {
	case s.taskQueue <- task: // thread save
		return taskID, nil
	default:
		// The channel is full. Refuse the job instead of hanging.
		return "", fmt.Errorf("[Error] Scheduler queue is at capacity")
	}

}

// gRPC streaming handlers will just call this in a loop.
func (s *Scheduler) GetNextTask(ctx context.Context) (*Task, error) {
	select {
	case task := <-s.taskQueue:
		// We got a task --> Now safely record it as active.
		s.mu.Lock()
		s.activeTasks[task.ID] = task
		s.mu.Unlock()
		return task, nil

	case <-ctx.Done():
		// The worker disconnected or the server is shutting down
		return nil, ctx.Err()
	}
}

// Safely removes and returns the task
func (s *Scheduler) popActiveTask(taskID string) (*Task, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	task, exists := s.activeTasks[taskID]
	if exists {
		delete(s.activeTasks, taskID)
	}
	return task, exists
}

// Call this if a worker drops off.
func (s *Scheduler) RequeueTask(taskID string) {
	task, exists := s.popActiveTask(taskID)

	// Put the task back into the scheduler queue
	if exists {
		task.CreatedAt = time.Now()

		// Push it back onto the queue for another worker to grab.
		// We do this in a goroutine so it doesn't block if the queue is temporarily full.
		go func(t *Task) (string, error) {
			select {
			case s.taskQueue <- t:
				return t.ID, nil
			default:
				// The channel is full! Refuse the job instead of hanging.
				return "", fmt.Errorf("scheduler queue is at capacity")
			}
		}(task)
	}
}

// MarkTaskComplete removes it from active tracking without doing anything with the values
func (s *Scheduler) MarkTaskComplete(taskID string) {
	s.popActiveTask(taskID)
}

// GetQueueLength returns current queue size for KEDA. NO LOCKS NEEDED.
func (s *Scheduler) GetQueueLength() int {
	// len() on a buffered channel is thread-safe!
	return len(s.taskQueue)
}

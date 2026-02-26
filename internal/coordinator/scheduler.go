package coordinator

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/google/uuid"
)

type Task struct {
	ID      string
	JobID   string
	FrameID string
	Payload interface{} // *pb.RenderTask or *pb.CompositeTask

	CreatedAt time.Time
	StartedAt time.Time // WHEN the worker pulled it
	Retries   int32     // How many times it has been requeued
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

		task.StartedAt = time.Now()
		s.activeTasks[task.ID] = task

		s.mu.Unlock()
		return task, nil

	case <-ctx.Done():
		// The worker pod is disconnected by GKE or the server is shutting down
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
				return "", fmt.Errorf("[SCHEDULER] Scheduler queue is at capacity")
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

// StartSweeper runs a background loop to reclaim tasks from dead workers (that may have segfaulted).
// Call this once right after creating the Scheduler: `go scheduler.StartSweeper(ctx, ...)`
func (s *Scheduler) StartSweeper(ctx context.Context, timeout time.Duration, checkInterval time.Duration) {
	ticker := time.NewTicker(checkInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return // Server is shutting down, stop sweeping
		case <-ticker.C:
			s.sweep(timeout)
		}
	}
}

func (s *Scheduler) sweep(timeout time.Duration) {
	now := time.Now()

	// Create a temporary list to hold tasks we need to recover
	var deadTasks []*Task

	// Lock just long enough to scan the map and remove the bad entries
	s.mu.Lock()
	for id, task := range s.activeTasks {
		// TODO: Change StartedAt to LastHeartbeat if we implement
		if now.Sub(task.StartedAt) > timeout {
			deadTasks = append(deadTasks, task)
			delete(s.activeTasks, id) // Remove it from active tracking
		}
	}
	s.mu.Unlock()

	// Now we are outside the lock. The rest of the server can keep running.
	// We can safely process the requeues without freezing the scheduler.
	for _, task := range deadTasks {
		task.Retries++

		if task.Retries > 3 {
			fmt.Printf("[SCHEDULER] Task %s failed %d times. Dropping it permanently.\n", task.ID, task.Retries)
			continue
		}

		// Push it back to the queue
		select {
		case s.taskQueue <- task:
			fmt.Printf("[SCHEDULER] Worker timeout! Requeued task %s (Retry %d/3)\n", task.ID, task.Retries)
		default:
			fmt.Printf("[SCHEDULER] Queue is full! Lost timed-out task %s\n", task.ID)
		}
	}
}

func (s *Scheduler) PurgeJobTasks(jobID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	for taskID, task := range s.activeTasks {
		if task.JobID == jobID {
			delete(s.activeTasks, taskID)
		}
	}
	return nil
}

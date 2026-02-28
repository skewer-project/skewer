package coordinator

import (
	"context"
	"fmt"
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

	CreatedAt time.Time
	StartedAt time.Time // WHEN the worker pulled it
	Retries   int32     // How many times it has been requeued
}

type Scheduler struct {
	mu          sync.Mutex
	skewerQueue chan *Task       // Only for Render Tasks (thread safe)
	loomQueue   chan *Task       // Only for Merge/Composite Tasks (thread safe)
	activeTasks map[string]*Task // Tasks currently being worked on
}

func NewScheduler(maxQueueSize int) *Scheduler {
	return &Scheduler{
		skewerQueue: make(chan *Task, maxQueueSize),
		loomQueue:   make(chan *Task, maxQueueSize),
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

	// Figure out which queue to put it in, and do it atomically
	switch payload.(type) {
	case *pb.RenderTask:
		select {
		case s.skewerQueue <- task:
			return taskID, nil
		default:
			return "", fmt.Errorf("[Error] Skewer queue is at capacity")
		}
	case *pb.MergeTask, *pb.CompositeTask:
		select {
		case s.loomQueue <- task:
			return taskID, nil
		default:
			return "", fmt.Errorf("[Error] Loom queue is at capacity")
		}
	default:
		return "", fmt.Errorf("[Error] Unknown task payload type")
	}

}

// gRPC streaming handlers will just call this in a loop.
func (s *Scheduler) GetNextTask(ctx context.Context, capabilities []string) (*Task, error) {

	if len(capabilities) == 0 {
		return nil, fmt.Errorf("[ERROR] No capabilities provided")
	}

	workerType := "none"
	for _, capability := range capabilities {
		if capability == "skewer" || capability == "loom" {
			workerType = capability
			break
		}
	}

	if workerType == "none" {
		return nil, fmt.Errorf("[ERROR] No compatible worker type found")
	}

	// Get next type based on workerType
	for {
		switch workerType {
		case "skewer":
			select {
			case task := <-s.skewerQueue: // Pull ONLY from Skewer Queue

				s.mu.Lock()
				task.StartedAt = time.Now()
				s.activeTasks[task.ID] = task
				s.mu.Unlock()

				return task, nil

			case <-ctx.Done():
				return nil, ctx.Err()
			}

		case "loom":
			select {
			case task := <-s.loomQueue: // Pull ONLY from Loom Queue

				s.mu.Lock()
				task.StartedAt = time.Now()
				s.activeTasks[task.ID] = task
				s.mu.Unlock()

				return task, nil

			case <-ctx.Done():
				return nil, ctx.Err()
			}
		}
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

func (s *Scheduler) RequeueTask(taskID string) {
	task, exists := s.popActiveTask(taskID)

	if exists {
		task.CreatedAt = time.Now()

		// Push it back onto the correct queue in a goroutine
		go func(t *Task) {
			switch t.Payload.(type) {

			// Route to Skewer Queue
			case *pb.RenderTask:
				select {
				case s.skewerQueue <- t:
					fmt.Printf("[SCHEDULER] Requeued Skewer task %s\n", t.ID)
				default:
					fmt.Printf("[SCHEDULER] Skewer queue is full. Lost task %s\n", t.ID)
				}

			// Route to Loom Queue
			case *pb.MergeTask, *pb.CompositeTask:
				select {
				case s.loomQueue <- t:
					fmt.Printf("[SCHEDULER] Requeued Loom task %s\n", t.ID)
				default:
					fmt.Printf("[SCHEDULER] Loom queue is full. Lost task %s\n", t.ID)
				}
			}
		}(task)
	}
}

// MarkTaskComplete removes it from active tracking without doing anything with the values
func (s *Scheduler) MarkTaskComplete(taskID string) (*Task, bool) {
	return s.popActiveTask(taskID)
}

// GetQueueLength returns current queue size for KEDA. NO LOCKS NEEDED.
func (s *Scheduler) GetQueueLength() int {
	return len(s.skewerQueue) + len(s.loomQueue)
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
		// Figure out which queue the dead task belongs to!
		switch task.Payload.(type) {

		case *pb.RenderTask:
			select {
			case s.skewerQueue <- task:
				fmt.Printf("[SCHEDULER] Worker timeout! Requeued Skewer task %s (Retry %d/3)\n", task.ID, task.Retries)
			default:
				fmt.Printf("[SCHEDULER] Skewer queue is full! Lost timed-out task %s\n", task.ID)
			}

		case *pb.MergeTask, *pb.CompositeTask:
			select {
			case s.loomQueue <- task:
				fmt.Printf("[SCHEDULER] Worker timeout! Requeued Loom task %s (Retry %d/3)\n", task.ID, task.Retries)
			default:
				fmt.Printf("[SCHEDULER] Loom queue is full! Lost timed-out task %s\n", task.ID)
			}
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

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

	OnTaskFailedPermanently func(task *Task) // Callback when a task is lost
}

func NewScheduler(maxQueueSize int) *Scheduler {
	return &Scheduler{
		skewerQueue: make(chan *Task, maxQueueSize),
		loomQueue:   make(chan *Task, maxQueueSize),
		activeTasks: make(map[string]*Task),
	}
}

// EnqueueTask adds a new task to the queue
func (s *Scheduler) EnqueueTask(payload any, jobID string, frameID string) (string, error) {
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
		s.skewerQueue <- task
		return taskID, nil

	case *pb.CompositeTask:
		s.loomQueue <- task
		return taskID, nil

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

// Returns task pointer with given ID from activeTasks
func (s *Scheduler) Task(taskID string) (*Task, bool) {
	task, exists := s.activeTasks[taskID]

	return task, exists
}

// Safely removes and returns the task
func (s *Scheduler) popActiveTask(taskID string) (*Task, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	task, exists := s.Task(taskID)
	if exists {
		delete(s.activeTasks, taskID)
	}
	return task, exists
}

func (s *Scheduler) RequeueTask(taskID string) {
	task, exists := s.popActiveTask(taskID)

	if exists {
		task.CreatedAt = time.Now()
		task.Retries++

		if task.Retries > 3 {
			fmt.Printf("[SCHEDULER] Task %s failed %d times. Dropping it permanently.\n", task.ID, task.Retries)

			if s.OnTaskFailedPermanently != nil {
				s.OnTaskFailedPermanently(task)
			}
			return
		}

		// Push it back onto the correct queue in a goroutine
		go func(t *Task) {
			switch t.Payload.(type) {

			// Route to Skewer Queue
			case *pb.RenderTask:
				s.skewerQueue <- t
				fmt.Printf("[SCHEDULER] Requeued Skewer task %s\n", t.ID)

			// Route to Loom Queue
			case *pb.CompositeTask:
				s.loomQueue <- t
				fmt.Printf("[SCHEDULER] Requeued Loom task %s\n", t.ID)
			}
		}(task)
	}
}

// MarkTaskComplete removes it from active tracking without doing anything with the values
func (s *Scheduler) MarkTaskComplete(taskID string) (*Task, bool) {
	return s.popActiveTask(taskID)
}

// GetQueueLength returns current total queue size for KEDA. NO LOCKS NEEDED.
func (s *Scheduler) GetQueueLength() int {
	return len(s.skewerQueue) + len(s.loomQueue)
}

func (s *Scheduler) GetQueueLengths() map[string]int {
	return map[string]int{
		"skewer": len(s.skewerQueue),
		"loom":   len(s.loomQueue),
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

			if s.OnTaskFailedPermanently != nil {
				s.OnTaskFailedPermanently(task)
			}
			continue
		}

		// Push it back to the queue
		// Figure out which queue the dead task belongs to!
		switch task.Payload.(type) {

		case *pb.RenderTask:
			s.skewerQueue <- task
			fmt.Printf("[SCHEDULER] Worker timeout! Requeued Skewer task %s (Retry %d/3)\n", task.ID, task.Retries)

		case *pb.CompositeTask:
			s.loomQueue <- task
			fmt.Printf("[SCHEDULER] Worker timeout! Requeued Loom task %s (Retry %d/3)\n", task.ID, task.Retries)
		}
	}
}

func (s *Scheduler) PurgeJobTasks(jobID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Purge all active tasks
	for taskID, task := range s.activeTasks {
		if task.JobID == jobID {
			delete(s.activeTasks, taskID)
		}
	}

	// Helper to filter tasks for a given job ID out of a queue channel.
	filterQueue := func(ch chan *Task, jobID string) {

		// Iterate to current length of ch
		qLen := len(ch)
		for i := 0; i < qLen; i++ {
			select {
			case task := <-ch:
				if task == nil {
					// Keep in case it's being used as sentinel or signal
					ch <- task
					continue
				}
				if task.JobID == jobID {
					// Drop task for job and don't reenqueue
					continue
				}

				// Reenqueue any other tasks
				ch <- task

			default:
				// Queue is empty
				return
			}
		}
	}

	// Use helper function to purge
	if s.skewerQueue != nil {
		filterQueue(s.skewerQueue, jobID)
	}
	if s.loomQueue != nil {
		filterQueue(s.loomQueue, jobID)
	}

	return nil
}

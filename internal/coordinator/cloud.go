package coordinator

type CloudManager interface {
	EnsureCapacity(workerCount int) error
	ProvisionStorage(projectID string) error
}

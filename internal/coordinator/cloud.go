package coordinator

import (
	"context"
	"fmt"
	"log"

	"cloud.google.com/go/storage"
	"google.golang.org/api/option"
)

type CloudStorageManager interface {
	ProvisionStorage(ctx context.Context, bucketName string) error
	IsCloudMode() bool
}

type GCPStorageManager struct {
	storageClient   *storage.Client
	credentialsFile string
}

// NewGCPStorageManager initializes a new CloudStorageManager using GCP credentials.
func NewGCPStorageManager(ctx context.Context, credentialsFile string) (*GCPStorageManager, error) {
	var storageClient *storage.Client
	var err error

	if credentialsFile != "" {
		log.Printf("[CLOUD]: Initializing GCP Storage client with provided credentials: %s", credentialsFile)
		// Use the modern WithAuthCredentialsFile option
		storageClient, err = storage.NewClient(ctx, option.WithAuthCredentialsFile(option.ServiceAccount, credentialsFile))
		if err != nil {
			return nil, fmt.Errorf("failed to initialize GCP storage client: %w", err)
		}
	} else {
		log.Printf("[CLOUD]: No GCP credentials provided. Running in local mode.")
	}

	return &GCPStorageManager{
		storageClient:   storageClient,
		credentialsFile: credentialsFile,
	}, nil
}

// IsCloudMode returns true if the coordinator is authenticated to Google Cloud.
func (c *GCPStorageManager) IsCloudMode() bool {
	return c.credentialsFile != "" || c.storageClient != nil
}

// ProvisionStorage verifies access to the user's GCP bucket and configures storage.
func (c *GCPStorageManager) ProvisionStorage(ctx context.Context, bucketName string) error {
	if c.storageClient == nil {
		return fmt.Errorf("[ERROR]: GCP storage client not initialized; missing credentials")
	}

	log.Printf("[CLOUD]: Verifying storage access to bucket: gs://%s", bucketName)

	// A simple check to ensure we can list objects or get bucket metadata.
	bucket := c.storageClient.Bucket(bucketName)
	attrs, err := bucket.Attrs(ctx)
	if err != nil {
		return fmt.Errorf("[ERROR]: Failed to access user bucket (gs://%s): %w", bucketName, err)
	}

	log.Printf("[CLOUD]: Successfully verified access to target bucket gs://%s (Location: %s)", attrs.Name, attrs.Location)
	return nil
}

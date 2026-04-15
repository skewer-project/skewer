package coordinator

import (
	"context"
	"crypto/sha256"
	"fmt"
	"io"

	"cloud.google.com/go/storage"
)

// ComputeLayerCacheKey downloads the scene JSON from GCS and returns its
// SHA-256 hash as a hex string. The scene file already encodes all render
// parameters (resolution, samples, noise threshold, etc.), so hashing the
// file content is sufficient as a stable cache key.
func ComputeLayerCacheKey(ctx context.Context, client *storage.Client, sceneURI string) (string, error) {
	bucketName, objectPath, err := sceneURIToBucketObject(sceneURI)
	if err != nil {
		return "", err
	}

	r, err := client.Bucket(bucketName).Object(objectPath).NewReader(ctx)
	if err != nil {
		return "", fmt.Errorf("open scene object %s: %w", sceneURI, err)
	}
	defer r.Close()

	h := sha256.New()
	if _, err := io.Copy(h, r); err != nil {
		return "", fmt.Errorf("read scene object %s: %w", sceneURI, err)
	}

	return fmt.Sprintf("%x", h.Sum(nil)), nil
}

package coordinator

import (
	"context"
	"crypto/sha256"
	"fmt"
	"io"

	"cloud.google.com/go/storage"
)

// ComputeLayerCacheKey produces a stable SHA-256 fingerprint for one render layer.
// The key covers everything that, if changed, would invalidate the cached output:
//   - root scene.json bytes (camera, animation timeline, layer ordering)
//   - each context file's bytes (lighting, invisible geometry)
//   - the target layer file's bytes (geometry, materials, animation)
//   - renderer image identifier (encodes binary version)
//   - render mode: "static" or "animated"
func ComputeLayerCacheKey(ctx context.Context, client *storage.Client, sceneURI, layerURI string, contextURIs []string, rendererImage, mode string) (string, error) {
	h := sha256.New()

	if err := hashGCSObject(ctx, client, sceneURI, h); err != nil {
		return "", fmt.Errorf("hashing scene %s: %w", sceneURI, err)
	}
	for _, ctxURI := range contextURIs {
		if err := hashGCSObject(ctx, client, ctxURI, h); err != nil {
			return "", fmt.Errorf("hashing context %s: %w", ctxURI, err)
		}
	}
	if err := hashGCSObject(ctx, client, layerURI, h); err != nil {
		return "", fmt.Errorf("hashing layer %s: %w", layerURI, err)
	}

	// Mix in renderer version and render mode so a binary upgrade or
	// static↔animated reclassification both produce a cache miss.
	fmt.Fprintf(h, "\x00renderer=%s\x00mode=%s", rendererImage, mode)

	return fmt.Sprintf("%x", h.Sum(nil)), nil
}

func hashGCSObject(ctx context.Context, client *storage.Client, uri string, h io.Writer) error {
	bucket, object, err := sceneURIToBucketObject(uri)
	if err != nil {
		return err
	}
	r, err := client.Bucket(bucket).Object(object).NewReader(ctx)
	if err != nil {
		return fmt.Errorf("open %s: %w", uri, err)
	}
	defer r.Close()
	if _, err := io.Copy(h, r); err != nil {
		return fmt.Errorf("read %s: %w", uri, err)
	}
	return nil
}

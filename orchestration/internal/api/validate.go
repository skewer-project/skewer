package api

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"path"
	"strings"

	"cloud.google.com/go/storage"
)

// SceneValidator reads an uploaded scene + layer files and verifies the
// metadata expected by the coordinator is already present. It does not mutate
// uploaded scene data.
type SceneValidator struct {
	client     *storage.Client
	dataBucket string
}

func NewSceneValidator(client *storage.Client, dataBucket string) *SceneValidator {
	return &SceneValidator{client: client, dataBucket: dataBucket}
}

// Validate checks the uploaded bundle in place:
//  1. Downloads <uploadPrefix>/<scenePath>
//  2. Requires an explicit top-level animation block.
//  3. For every layer+context file it references, downloads and verifies an
//     explicit animated field.
//
// Returns the original scene.json GCS URI.
func (v *SceneValidator) Validate(ctx context.Context, uploadPrefix, scenePath string) (string, error) {
	cleanedScene, err := SanitizeObjectPath(scenePath)
	if err != nil {
		return "", fmt.Errorf("scene_path: %w", err)
	}
	scenePath = cleanedScene
	sceneObject := path.Join(uploadPrefix, scenePath)

	sceneBytes, err := v.readObject(ctx, sceneObject)
	if err != nil {
		return "", fmt.Errorf("read scene %s: %w", sceneObject, err)
	}

	var sceneMap map[string]any
	if err := json.Unmarshal(sceneBytes, &sceneMap); err != nil {
		return "", fmt.Errorf("parse scene JSON: %w", err)
	}
	if err := validateAnimationBlock(sceneMap["animation"]); err != nil {
		return "", err
	}

	// Directory within the upload prefix that contains the scene.json, so we
	// can resolve layer/context paths relative to it (matches the C++ and
	// coordinator-side resolution rules).
	sceneDir := path.Dir(path.Join(uploadPrefix, scenePath))

	layerRefs, err := requireStringSlice(sceneMap, "layers")
	if err != nil {
		return "", err
	}
	contextRefs, err := optionalStringSlice(sceneMap, "context")
	if err != nil {
		return "", err
	}

	for _, rel := range append(append([]string{}, layerRefs...), contextRefs...) {
		object, err := resolveSceneMemberObject(sceneDir, rel)
		if err != nil {
			return "", err
		}
		if err := v.validateLayerLikeFile(ctx, object); err != nil {
			return "", fmt.Errorf("validate scene member %s: %w", object, err)
		}
	}

	return fmt.Sprintf("gs://%s/%s", v.dataBucket, sceneObject), nil
}

func validateAnimationBlock(raw any) error {
	animation, ok := raw.(map[string]any)
	if !ok {
		return fmt.Errorf("scene animation block is required")
	}
	for _, key := range []string{"start", "end", "fps"} {
		if _, ok := animation[key].(float64); !ok {
			return fmt.Errorf("scene animation.%s must be a number", key)
		}
	}
	if fps := animation["fps"].(float64); fps <= 0 {
		return fmt.Errorf("scene animation.fps must be positive")
	}
	if end := animation["end"].(float64); end < animation["start"].(float64) {
		return fmt.Errorf("scene animation.end must be >= animation.start")
	}
	shutterAngle, ok := animation["shutter_angle"].(float64)
	if !ok {
		return fmt.Errorf("scene animation.shutter_angle must be a number")
	}
	if shutterAngle <= 0 || shutterAngle > 360 {
		return fmt.Errorf("scene animation.shutter_angle must be in (0, 360]")
	}
	return nil
}

// validateLayerLikeFile reads a single layer/context JSON and verifies the explicit
// animated field used by the coordinator's layer-mode decision is present.
func (v *SceneValidator) validateLayerLikeFile(ctx context.Context, object string) error {
	data, err := v.readObject(ctx, object)
	if err != nil {
		return err
	}
	var layerMap map[string]any
	if err := json.Unmarshal(data, &layerMap); err != nil {
		return fmt.Errorf("parse layer JSON: %w", err)
	}

	if _, ok := layerMap["animated"].(bool); !ok {
		return fmt.Errorf("layer animated field is required")
	}

	return nil
}

func requireStringSlice(m map[string]any, key string) ([]string, error) {
	out, err := optionalStringSlice(m, key)
	if err != nil {
		return nil, err
	}
	if len(out) == 0 {
		return nil, fmt.Errorf("scene %s must include at least one entry", key)
	}
	return out, nil
}

func optionalStringSlice(m map[string]any, key string) ([]string, error) {
	v, present := m[key]
	if !present {
		return nil, nil
	}
	raw, ok := v.([]any)
	if !ok {
		return nil, fmt.Errorf("scene %s must be an array of strings", key)
	}
	out := make([]string, 0, len(raw))
	for i, el := range raw {
		s, ok := el.(string)
		if !ok || s == "" {
			return nil, fmt.Errorf("scene %s[%d] must be a non-empty string", key, i)
		}
		out = append(out, s)
	}
	return out, nil
}

// resolveSceneMemberObject joins a relative layer/context path onto the
// scene's directory. Upload bundles only support relative member references.
func resolveSceneMemberObject(sceneDir, ref string) (string, error) {
	cleanRef, err := SanitizeObjectPath(ref)
	if err != nil {
		return "", fmt.Errorf("scene member path %q: %w", ref, err)
	}
	object := path.Join(sceneDir, cleanRef)
	if !isPathWithin(object, sceneDir) {
		return "", fmt.Errorf("scene member path %q escapes scene directory", ref)
	}
	return object, nil
}

func isPathWithin(object, dir string) bool {
	cleanDir := path.Clean(dir)
	cleanObject := path.Clean(object)
	return strings.HasPrefix(cleanObject, cleanDir+"/")
}

func (v *SceneValidator) readObject(ctx context.Context, object string) ([]byte, error) {
	r, err := v.client.Bucket(v.dataBucket).Object(object).NewReader(ctx)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", object, err)
	}
	defer r.Close()
	return io.ReadAll(r)
}

package api

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"path"
	"strings"

	"cloud.google.com/go/storage"
)

const (
	defaultFPS          = 24.0
	defaultShutterAngle = 180.0
)

// Normalizer reads an uploaded scene + layer files, auto-fills the top-level
// `animation` block and per-layer `animated` flags expected by the coordinator,
// and writes the normalized files back to GCS.
//
// The logic mirrors apps/scene-previewer/src/services/transform.ts
// (getAnimationRange + collectSceneKeyframeTimes) so that API-side
// normalization matches the previewer's behavior on the same scene data.
type Normalizer struct {
	client     *storage.Client
	dataBucket string
}

func NewNormalizer(client *storage.Client, dataBucket string) *Normalizer {
	return &Normalizer{client: client, dataBucket: dataBucket}
}

// Normalize mutates the uploaded bundle in place:
//  1. Downloads <uploadPrefix>/<scenePath>
//  2. For every layer+context file it references, downloads and inspects it
//     for keyframes, writing back with an `animated` field if one was missing.
//  3. Derives {start, end} from the union of keyframe times, writes back the
//     scene.json with an `animation` block (fps + shutter_angle default to
//     24 / 180°) if one was not already present.
//
// Returns the final scene.json GCS URI.
func (n *Normalizer) Normalize(ctx context.Context, uploadPrefix, scenePath string) (string, error) {
	cleanedScene, err := SanitizeObjectPath(scenePath)
	if err != nil {
		return "", fmt.Errorf("scene_path: %w", err)
	}
	scenePath = cleanedScene
	sceneObject := path.Join(uploadPrefix, scenePath)

	sceneBytes, err := n.readObject(ctx, sceneObject)
	if err != nil {
		return "", fmt.Errorf("read scene %s: %w", sceneObject, err)
	}

	var sceneMap map[string]any
	if err := json.Unmarshal(sceneBytes, &sceneMap); err != nil {
		return "", fmt.Errorf("parse scene JSON: %w", err)
	}

	// Directory within the upload prefix that contains the scene.json, so we
	// can resolve layer/context paths relative to it (matches the C++ and
	// coordinator-side resolution rules).
	sceneDir := path.Dir(path.Join(uploadPrefix, scenePath))

	layerRefs := extractStringSlice(sceneMap["layers"])
	contextRefs := extractStringSlice(sceneMap["context"])

	allTimes := []float64{}
	for _, rel := range append(append([]string{}, layerRefs...), contextRefs...) {
		object := resolveLayerObject(sceneDir, rel)
		times, err := n.normalizeLayer(ctx, object)
		if err != nil {
			return "", fmt.Errorf("normalize layer %s: %w", object, err)
		}
		allTimes = append(allTimes, times...)
	}

	if _, present := sceneMap["animation"]; !present {
		sceneMap["animation"] = buildAnimationBlock(allTimes)
	}

	newScene, err := json.MarshalIndent(sceneMap, "", "  ")
	if err != nil {
		return "", fmt.Errorf("marshal normalized scene: %w", err)
	}

	if err := n.writeObject(ctx, sceneObject, newScene, "application/json"); err != nil {
		return "", fmt.Errorf("write normalized scene: %w", err)
	}

	return fmt.Sprintf("gs://%s/%s", n.dataBucket, sceneObject), nil
}

// normalizeLayer reads a single layer JSON, ensures it has an `animated`
// field, writes it back if we added one, and returns every keyframe time
// that appeared in its graph.
func (n *Normalizer) normalizeLayer(ctx context.Context, object string) ([]float64, error) {
	data, err := n.readObject(ctx, object)
	if err != nil {
		return nil, err
	}
	var layerMap map[string]any
	if err := json.Unmarshal(data, &layerMap); err != nil {
		return nil, fmt.Errorf("parse layer JSON: %w", err)
	}

	var times []float64
	if graph, ok := layerMap["graph"].([]any); ok {
		for _, node := range graph {
			times = append(times, collectKeyframeTimes(node)...)
		}
	}
	hasKeyframes := len(times) > 0

	mutated := false
	if _, present := layerMap["animated"]; !present {
		layerMap["animated"] = hasKeyframes
		mutated = true
	}

	if mutated {
		newLayer, err := json.MarshalIndent(layerMap, "", "  ")
		if err != nil {
			return nil, fmt.Errorf("marshal normalized layer: %w", err)
		}
		if err := n.writeObject(ctx, object, newLayer, "application/json"); err != nil {
			return nil, fmt.Errorf("write normalized layer: %w", err)
		}
	}

	return times, nil
}

// collectKeyframeTimes walks a node (group or leaf) and returns every
// keyframe.time value inside AnimatedTransforms anywhere in the subtree.
// Mirrors visitSceneNodes + isAnimated + for-each-keyframe in transform.ts.
func collectKeyframeTimes(node any) []float64 {
	m, ok := node.(map[string]any)
	if !ok {
		return nil
	}

	var out []float64

	if transform, ok := m["transform"].(map[string]any); ok {
		if kfs, ok := transform["keyframes"].([]any); ok {
			for _, kf := range kfs {
				kfMap, ok := kf.(map[string]any)
				if !ok {
					continue
				}
				if t, ok := kfMap["time"].(float64); ok {
					out = append(out, t)
				}
			}
		}
	}

	if children, ok := m["children"].([]any); ok {
		for _, child := range children {
			out = append(out, collectKeyframeTimes(child)...)
		}
	}

	return out
}

func buildAnimationBlock(times []float64) map[string]any {
	if len(times) == 0 {
		return map[string]any{
			"start":         0.0,
			"end":           0.0,
			"fps":           defaultFPS,
			"shutter_angle": defaultShutterAngle,
		}
	}
	start := times[0]
	end := times[0]
	for _, t := range times[1:] {
		start = math.Min(start, t)
		end = math.Max(end, t)
	}
	return map[string]any{
		"start":         start,
		"end":           end,
		"fps":           defaultFPS,
		"shutter_angle": defaultShutterAngle,
	}
}

func extractStringSlice(v any) []string {
	raw, ok := v.([]any)
	if !ok {
		return nil
	}
	out := make([]string, 0, len(raw))
	for _, el := range raw {
		if s, ok := el.(string); ok {
			out = append(out, s)
		}
	}
	return out
}

// resolveLayerObject joins a relative layer path onto the scene's directory.
// Absolute gs:// URIs in layer refs are *not* supported in the previewer
// bundle format, so we just treat everything as relative.
func resolveLayerObject(sceneDir, ref string) string {
	if strings.HasPrefix(ref, "gs://") {
		// Strip scheme+bucket; surface the object path unchanged.
		trimmed := strings.TrimPrefix(ref, "gs://")
		if idx := strings.IndexByte(trimmed, '/'); idx >= 0 {
			return trimmed[idx+1:]
		}
		return trimmed
	}
	return path.Join(sceneDir, ref)
}

func (n *Normalizer) readObject(ctx context.Context, object string) ([]byte, error) {
	r, err := n.client.Bucket(n.dataBucket).Object(object).NewReader(ctx)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", object, err)
	}
	defer r.Close()
	return io.ReadAll(r)
}

func (n *Normalizer) writeObject(ctx context.Context, object string, data []byte, contentType string) error {
	w := n.client.Bucket(n.dataBucket).Object(object).NewWriter(ctx)
	w.ContentType = contentType
	if _, err := w.Write(data); err != nil {
		_ = w.Close()
		return fmt.Errorf("write %s: %w", object, err)
	}
	return w.Close()
}

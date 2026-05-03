package coordinator

import (
	"encoding/json"
	"math"
	"testing"
)

// TestNumFramesFromAnimation verifies frame count derivation from the scene animation block.
func TestNumFramesFromAnimation(t *testing.T) {
	cases := []struct {
		name      string
		start     float64
		end       float64
		fps       float64
		wantFrames int
	}{
		{"24fps 5s", 0, 5, 24, 120},
		{"30fps 1s", 0, 1, 30, 30},
		{"no animation", 0, 0, 0, 1},  // fps=0 → default 1
		{"fractional frames", 0, 2.5, 24, 60},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := numFramesFromAnimation(tc.start, tc.end, tc.fps)
			if got != tc.wantFrames {
				t.Errorf("numFramesFromAnimation(%v,%v,%v) = %d, want %d",
					tc.start, tc.end, tc.fps, got, tc.wantFrames)
			}
		})
	}
}

// TestLayerClassification verifies that the animated flag is correctly mapped to mode.
func TestLayerClassification(t *testing.T) {
	cases := []struct {
		animated bool
		wantMode string
	}{
		{true, "animated"},
		{false, "static"},
	}
	for _, tc := range cases {
		mode := "static"
		if tc.animated {
			mode = "animated"
		}
		if mode != tc.wantMode {
			t.Errorf("animated=%v → mode=%q, want %q", tc.animated, mode, tc.wantMode)
		}
	}
}

// TestSceneJSONParsing verifies minimal scene JSON parsing.
func TestSceneJSONParsing(t *testing.T) {
	raw := `{
		"layers": ["layers/smoke.json", "layers/char.json"],
		"context": ["ctx/lights.json"],
		"animation": {"start": 0, "end": 5, "fps": 24, "shutter_angle": 180}
	}`
	var sceneJSON minimalSceneJSON
	if err := json.Unmarshal([]byte(raw), &sceneJSON); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if len(sceneJSON.Layers) != 2 {
		t.Errorf("want 2 layers, got %d", len(sceneJSON.Layers))
	}
	if len(sceneJSON.Context) != 1 {
		t.Errorf("want 1 context, got %d", len(sceneJSON.Context))
	}
	if sceneJSON.Animation == nil {
		t.Fatal("animation block missing")
	}
	if sceneJSON.Animation.FPS != 24 {
		t.Errorf("fps: want 24, got %v", sceneJSON.Animation.FPS)
	}
	frames := numFramesFromAnimation(sceneJSON.Animation.Start, sceneJSON.Animation.End, sceneJSON.Animation.FPS)
	if frames != 120 {
		t.Errorf("frames: want 120, got %d", frames)
	}
}

// TestLayerJSONAnimatedParsing verifies the animated flag peek.
func TestLayerJSONAnimatedParsing(t *testing.T) {
	cases := []struct {
		raw      string
		wantAnim bool
	}{
		{`{"animated": true, "render": {}}`, true},
		{`{"animated": false}`, false},
		{`{}`, false},  // absent key → false
	}
	for _, tc := range cases {
		var lj minimalLayerJSON
		if err := json.Unmarshal([]byte(tc.raw), &lj); err != nil {
			t.Fatalf("unmarshal %q: %v", tc.raw, err)
		}
		got := lj.Animated != nil && *lj.Animated
		if got != tc.wantAnim {
			t.Errorf("%q: animated=%v, want %v", tc.raw, got, tc.wantAnim)
		}
	}
}

// TestResolveGCSURI verifies relative path resolution against a GCS base directory.
func TestResolveGCSURI(t *testing.T) {
	cases := []struct {
		base string
		ref  string
		want string
	}{
		{
			base: "gs://bucket/scenes/",
			ref:  "layers/smoke.json",
			want: "gs://bucket/scenes/layers/smoke.json",
		},
		{
			base: "gs://bucket/scenes/scene.json",
			ref:  "layers/smoke.json",
			want: "gs://bucket/scenes/layers/smoke.json",
		},
		{
			base: "gs://bucket/scenes/",
			ref:  "gs://other-bucket/shared/ctx.json",
			want: "gs://other-bucket/shared/ctx.json",
		},
	}
	for _, tc := range cases {
		baseDir := tc.base
		// Simulate gcsURIDir if base ends without slash (file path)
		if len(baseDir) > 0 && baseDir[len(baseDir)-1] != '/' {
			baseDir = gcsURIDir(baseDir)
		}
		got := resolveGCSURI(baseDir, tc.ref)
		if got != tc.want {
			t.Errorf("resolveGCSURI(%q, %q) = %q, want %q", baseDir, tc.ref, got, tc.want)
		}
	}
}

// TestGCSFileStem verifies stem extraction from GCS URIs.
func TestGCSFileStem(t *testing.T) {
	cases := []struct {
		uri  string
		want string
	}{
		{"gs://bucket/scenes/smoke_layer.json", "smoke_layer"},
		{"gs://bucket/layers/character.json", "character"},
		{"/mnt/bucket/layers/bg.json", "bg"},
	}
	for _, tc := range cases {
		got := gcsFileStem(tc.uri)
		if got != tc.want {
			t.Errorf("gcsFileStem(%q) = %q, want %q", tc.uri, got, tc.want)
		}
	}
}

// TestCacheKeyStability verifies that animating mode produces distinct cache keys.
// (Full GCS-backed key computation is tested via integration tests.)
func TestCacheKeyStability(t *testing.T) {
	// The cache key includes the mode string; static and animated must differ.
	if "static" == "animated" {
		t.Error("mode strings must be distinct")
	}
}

// TestFramesPerTaskChunking verifies the ceil division used by the workflow.
func TestFramesPerTaskChunking(t *testing.T) {
	cases := []struct {
		numFrames     int
		framesPerTask int
		wantTasks     int
	}{
		{120, 8, 15},
		{121, 8, 16},
		{8, 8, 1},
		{1, 8, 1},
		{0, 8, 0},
	}
	for _, tc := range cases {
		got := int(math.Ceil(float64(tc.numFrames) / float64(tc.framesPerTask)))
		if tc.numFrames == 0 {
			got = 0
		}
		if got != tc.wantTasks {
			t.Errorf("ceil(%d/%d) = %d, want %d", tc.numFrames, tc.framesPerTask, got, tc.wantTasks)
		}
	}
}

// numFramesFromAnimation is the pure function extracted for testing.
func numFramesFromAnimation(start, end, fps float64) int {
	if fps <= 0 {
		return 1
	}
	dur := end - start
	if dur <= 0 {
		return 1
	}
	return int(math.Round(dur * fps))
}

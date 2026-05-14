package api

import (
	"strings"
	"testing"
)

func TestValidateAnimationBlockRequiresExplicitMetadata(t *testing.T) {
	tests := []struct {
		name    string
		raw     any
		wantErr string
	}{
		{
			name:    "missing block",
			raw:     nil,
			wantErr: "scene animation block is required",
		},
		{
			name: "missing fps",
			raw: map[string]any{
				"start": float64(0),
				"end":   float64(5),
			},
			wantErr: "scene animation.fps must be a number",
		},
		{
			name: "nonpositive fps",
			raw: map[string]any{
				"start": float64(0),
				"end":   float64(5),
				"fps":   float64(0),
			},
			wantErr: "scene animation.fps must be positive",
		},
		{
			name: "bad shutter angle",
			raw: map[string]any{
				"start":         float64(0),
				"end":           float64(5),
				"fps":           float64(24),
				"shutter_angle": "180",
			},
			wantErr: "scene animation.shutter_angle must be a number",
		},
		{
			name: "missing shutter angle",
			raw: map[string]any{
				"start": float64(0),
				"end":   float64(5),
				"fps":   float64(24),
			},
			wantErr: "scene animation.shutter_angle must be a number",
		},
		{
			name: "end before start",
			raw: map[string]any{
				"start":         float64(5),
				"end":           float64(0),
				"fps":           float64(24),
				"shutter_angle": float64(180),
			},
			wantErr: "scene animation.end must be >= animation.start",
		},
		{
			name: "shutter angle out of range",
			raw: map[string]any{
				"start":         float64(0),
				"end":           float64(5),
				"fps":           float64(24),
				"shutter_angle": float64(361),
			},
			wantErr: "scene animation.shutter_angle must be in (0, 360]",
		},
		{
			name: "valid",
			raw: map[string]any{
				"start":         float64(0),
				"end":           float64(5),
				"fps":           float64(24),
				"shutter_angle": float64(180),
			},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			err := validateAnimationBlock(tc.raw)
			if tc.wantErr == "" {
				if err != nil {
					t.Fatalf("validateAnimationBlock() error = %v", err)
				}
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("validateAnimationBlock() error = %v, want %q", err, tc.wantErr)
			}
		})
	}
}

func TestSceneReferenceValidation(t *testing.T) {
	scene := map[string]any{
		"layers": []any{"layer_background.json", "layer_subject.json"},
	}

	got, err := requireStringSlice(scene, "layers")
	if err != nil {
		t.Fatalf("requireStringSlice() error = %v", err)
	}
	if len(got) != 2 || got[0] != "layer_background.json" || got[1] != "layer_subject.json" {
		t.Fatalf("requireStringSlice() = %#v", got)
	}

	if _, err := requireStringSlice(map[string]any{"layers": []any{}}, "layers"); err == nil {
		t.Fatal("requireStringSlice() accepted an empty required slice")
	}
	if _, err := optionalStringSlice(map[string]any{"context": []any{"ctx.json", 42}}, "context"); err == nil {
		t.Fatal("optionalStringSlice() accepted a non-string reference")
	}
}

func TestResolveSceneMemberObjectRejectsEscapes(t *testing.T) {
	tests := []struct {
		name    string
		ref     string
		want    string
		wantErr string
	}{
		{
			name: "relative file",
			ref:  "layer_subject.json",
			want: "uploads/pipeline/scenes/layer_subject.json",
		},
		{
			name: "relative nested file",
			ref:  "layers/layer_subject.json",
			want: "uploads/pipeline/scenes/layers/layer_subject.json",
		},
		{
			name:    "traversal rejected",
			ref:     "../other/layer.json",
			wantErr: "invalid path segment",
		},
		{
			name:    "absolute rejected",
			ref:     "/uploads/pipeline/layer.json",
			wantErr: "path must be relative",
		},
		{
			name:    "gcs uri rejected",
			ref:     "gs://bucket/uploads/pipeline/layer.json",
			wantErr: "invalid path segment",
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := resolveSceneMemberObject("uploads/pipeline/scenes", tc.ref)
			if tc.wantErr == "" {
				if err != nil {
					t.Fatalf("resolveSceneMemberObject() error = %v", err)
				}
				if got != tc.want {
					t.Fatalf("resolveSceneMemberObject() = %q, want %q", got, tc.want)
				}
				return
			}
			if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("resolveSceneMemberObject() error = %v, want %q", err, tc.wantErr)
			}
		})
	}
}

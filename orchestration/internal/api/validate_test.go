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

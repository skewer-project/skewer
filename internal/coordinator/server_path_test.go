package coordinator

import (
	"testing"
)

func TestGcsURIToFusePath_BasicBucket(t *testing.T) {
	out, err := gcsURIToFusePath("gs://my-bucket/scenes/cornell.json")
	if err != nil {
		t.Fatal(err)
	}
	want := "/gcs/my-bucket/scenes/cornell.json"
	if out != want {
		t.Fatalf("got %q want %q", out, want)
	}
}

func TestGcsURIToFusePath_TrailingSlash(t *testing.T) {
	out, err := gcsURIToFusePath("gs://my-bucket/renders/test/")
	if err != nil {
		t.Fatal(err)
	}
	want := "/gcs/my-bucket/renders/test/"
	if out != want {
		t.Fatalf("got %q want %q", out, want)
	}
}

func TestGcsURIToFusePath_BucketOnly(t *testing.T) {
	out, err := gcsURIToFusePath("gs://my-bucket")
	if err != nil {
		t.Fatal(err)
	}
	want := "/gcs/my-bucket"
	if out != want {
		t.Fatalf("got %q want %q", out, want)
	}
}

func TestGcsURIToFusePath_RejectsLocalPath(t *testing.T) {
	_, err := gcsURIToFusePath("scenes/cornell.json")
	if err == nil {
		t.Fatal("expected error for local path, got nil")
	}
}

func TestGcsURIToFusePath_RejectsAbsolutePath(t *testing.T) {
	_, err := gcsURIToFusePath("/data/scenes/cornell.json")
	if err == nil {
		t.Fatal("expected error for absolute path, got nil")
	}
}

func TestGcsURIToFusePath_RejectsHTTP(t *testing.T) {
	_, err := gcsURIToFusePath("http://example.com/file.json")
	if err == nil {
		t.Fatal("expected error for non-gs URI, got nil")
	}
}

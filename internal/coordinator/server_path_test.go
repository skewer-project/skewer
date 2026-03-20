package coordinator

import (
	"path/filepath"
	"testing"
)

func TestTranslateLocalPath_RepoRelativeScene(t *testing.T) {
	t.Setenv("LOCAL_DATA_PATH", "")
	s := &Server{localStorageBase: "/data"}

	out, err := s.translateLocalPath("scenes/cornell_box_fixed.json")
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join("/", "data", "scenes", "cornell_box_fixed.json")
	if out != want {
		t.Fatalf("got %q want %q", out, want)
	}
}

func TestTranslateLocalPath_DataPrefix(t *testing.T) {
	s := &Server{localStorageBase: "/data"}
	out, err := s.translateLocalPath("data/renders/")
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join("/", "data", "renders")
	if out != want {
		t.Fatalf("got %q want %q", out, want)
	}
}

func TestTranslateLocalPath_AbsoluteHostPath(t *testing.T) {
	host := "/host/repo"
	t.Setenv("LOCAL_DATA_PATH", host)
	s := &Server{localStorageBase: "/data"}
	abs := filepath.Join(host, "scenes", "foo.json")
	out, err := s.translateLocalPath(abs)
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join("/", "data", "scenes", "foo.json")
	if out != want {
		t.Fatalf("got %q want %q", out, want)
	}
}

func TestTranslateLocalPath_EmptyBasePassthrough(t *testing.T) {
	s := &Server{localStorageBase: ""}
	out, err := s.translateLocalPath("scenes/foo.json")
	if err != nil {
		t.Fatal(err)
	}
	if out != "scenes/foo.json" {
		t.Fatalf("got %q", out)
	}
}

func TestTranslateLocalPath_DotDotFooSegmentAllowed(t *testing.T) {
	s := &Server{localStorageBase: "/data"}
	out, err := s.translateLocalPath("..foo/bar.json")
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join("/", "data", "..foo", "bar.json")
	if out != want {
		t.Fatalf("got %q want %q", out, want)
	}
}

func TestTranslateLocalPath_ParentTraversalBlocked(t *testing.T) {
	s := &Server{localStorageBase: "/data"}
	_, err := s.translateLocalPath("../outside.json")
	if err == nil {
		t.Fatal("expected error for path traversal")
	}
}

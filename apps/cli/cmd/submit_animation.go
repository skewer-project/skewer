package cmd

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	coordinatorv1 "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	animSceneDir      string
	animUploadPrefix  string
	animOutputBucket  string
	animSceneName     string
	animMaxSamples    int32
	animWidth         int32
	animHeight        int32
	animThreads       int32
	animNoiseThresh   float32
	animMinSamples    int32
	animAdaptiveStep  int32
	animSkipUpload    bool
)

// animFrameScene is the minimal structure we parse from scene_NNNN.json files.
type animFrameScene struct {
	Layers []string `json:"layers"`
}

var submitAnimationCmd = &cobra.Command{
	Use:   "submit-animation",
	Short: "Submit a multi-frame animation: static layers rendered once, animated layers per-frame",
	Long: `Reads per-frame scene JSONs (scene_0001.json … scene_NNNN.json) from --scene-dir,
auto-detects which layers are static (identical across all frames) vs animated (unique per frame),
uploads the entire scene directory to GCS, then submits:
  • One RenderJob per static layer  (NumFrames=1, cached)
  • One RenderJob per animated layer (NumFrames=N, scene_####.json expansion)
  • One CompositeJob depending on all render jobs (NumFrames=N)

Output layout:
  gs://<bucket>/renders/<name>/layer_<stem>/frame-0001.exr     (static layer, once)
  gs://<bucket>/renders/<name>/layer_<stem>/frame-NNNN.exr     (animated layer, N frames)
  gs://<bucket>/renders/<name>/final/frame-NNNN.exr            (composited frames)
`,
	Run: func(cmd *cobra.Command, args []string) {
		// 1. Discover per-frame scene files: scene_0001.json … scene_NNNN.json
		framePattern := regexp.MustCompile(`^scene_(\d{4})\.json$`)
		entries, err := os.ReadDir(animSceneDir)
		if err != nil {
			log.Fatalf("Failed to read scene dir: %v", err)
		}

		type frameEntry struct {
			number int
			path   string
		}
		var frameFiles []frameEntry
		for _, e := range entries {
			if e.IsDir() {
				continue
			}
			m := framePattern.FindStringSubmatch(e.Name())
			if m == nil {
				continue
			}
			n := 0
			fmt.Sscanf(m[1], "%d", &n)
			frameFiles = append(frameFiles, frameEntry{n, filepath.Join(animSceneDir, e.Name())})
		}
		if len(frameFiles) == 0 {
			log.Fatalf("No scene_NNNN.json files found in %s", animSceneDir)
		}
		numFrames := len(frameFiles)
		fmt.Printf("[CLI] Found %d frames in %s\n", numFrames, animSceneDir)

		// 2. Parse every frame scene to build layer lists.
		//    layers[frameIdx][layerIdx] = filename (e.g. "layer_dodecahedron_0001.json")
		layersByFrame := make([][]string, numFrames)
		for i, ff := range frameFiles {
			data, err := os.ReadFile(ff.path)
			if err != nil {
				log.Fatalf("Failed to read %s: %v", ff.path, err)
			}
			var sc animFrameScene
			if err := json.Unmarshal(data, &sc); err != nil {
				log.Fatalf("Failed to parse %s: %v", ff.path, err)
			}
			if len(sc.Layers) == 0 {
				log.Fatalf("Frame scene %s has no layers", ff.path)
			}
			layersByFrame[i] = sc.Layers
		}

		// Validate all frames have the same number of layers.
		numLayers := len(layersByFrame[0])
		for i, layers := range layersByFrame {
			if len(layers) != numLayers {
				log.Fatalf("Frame %d has %d layers, expected %d", i+1, len(layers), numLayers)
			}
		}

		// 3. Classify each layer index as static or animated.
		//    Static: layer[i] is the same filename across ALL frames.
		//    Animated: layer[i] differs between at least two frames.
		isStatic := make([]bool, numLayers)
		for li := 0; li < numLayers; li++ {
			ref := layersByFrame[0][li]
			same := true
			for _, layers := range layersByFrame[1:] {
				if layers[li] != ref {
					same = false
					break
				}
			}
			isStatic[li] = same
		}

		for li := 0; li < numLayers; li++ {
			kind := "animated"
			if isStatic[li] {
				kind = "static"
			}
			fmt.Printf("[CLI] Layer %d (%s): %s\n", li, layersByFrame[0][li], kind)
		}

		// 4. Derive scene name.
		if animSceneName == "" {
			animSceneName = filepath.Base(animSceneDir)
		}

		// 5. Validate GCS prefixes.
		if !strings.HasPrefix(animUploadPrefix, "gs://") {
			log.Fatalf("--upload must be a gs:// URI, got: %s", animUploadPrefix)
		}
		if !strings.HasPrefix(animOutputBucket, "gs://") {
			log.Fatalf("--bucket must be a gs:// URI, got: %s", animOutputBucket)
		}

		outputPrefix := strings.TrimSuffix(animOutputBucket, "/") + "/renders/" + animSceneName + "/"

		// 6. Upload the entire scene directory to GCS (unless --skip-upload).
		if animSkipUpload {
			fmt.Printf("[CLI] Skipping upload (--skip-upload); using existing files at %s\n", animUploadPrefix)
		} else {
			fmt.Printf("[CLI] Uploading scene directory %s → %s\n", animSceneDir, animUploadPrefix)
			uploadedFiles := 0
			if err := uploadDirToGCS(animSceneDir, animUploadPrefix, &uploadedFiles); err != nil {
				log.Fatalf("Upload failed: %v", err)
			}
			fmt.Printf("[CLI] Uploaded %d files\n", uploadedFiles)
		}

		// The GCS base for this scene (used to construct scene URIs passed to the coordinator).
		gcsSceneBase := strings.TrimSuffix(animUploadPrefix, "/") + "/"

		// 7. Connect to coordinator.
		if err := ensureConnection(coordinatorAddr); err != nil {
			log.Fatalf("Error establishing connection: %v", err)
		}
		conn, err := grpc.NewClient(coordinatorAddr, grpc.WithTransportCredentials(insecure.NewCredentials()))
		if err != nil {
			log.Fatalf("Did not connect: %v", err)
		}
		defer conn.Close()
		c := coordinatorv1.NewCoordinatorServiceClient(conn)

		// 8. Submit render jobs.
		//    Static layers → SceneUri = scene_0001.json, NumFrames = 1
		//    Animated layers → SceneUri = scene_####.json, NumFrames = numFrames
		renderJobIDs := make([]string, numLayers)
		layerOutputPrefixes := make([]string, numLayers)
		staticLayerIndices := []int32{}

		for li := 0; li < numLayers; li++ {
			layerFile := layersByFrame[0][li] // name of the layer file (same for static, frame-1 for animated)
			layerStem := layerStemFromName(layerFile)
			layerOutputPrefix := outputPrefix + "layer_" + layerStem + "/"
			layerOutputPrefixes[li] = layerOutputPrefix

			var sceneURI string
			var nFrames int32
			if isStatic[li] {
				sceneURI = gcsSceneBase + frameFiles[0].path[len(animSceneDir)+1:] // scene_0001.json
				nFrames = 1
				staticLayerIndices = append(staticLayerIndices, int32(li))
			} else {
				// Replace the frame number in the first frame's scene filename with ####
				firstFrameName := filepath.Base(frameFiles[0].path)
				animScenePattern := framePattern.ReplaceAllString(firstFrameName, "scene_####.json")
				sceneURI = gcsSceneBase + animScenePattern
				nFrames = int32(numFrames)
			}

			// Allow ~500ms per frame for cache checks (GCS calls) plus a base of 30s.
			jobTimeout := 30*time.Second + time.Duration(nFrames)*500*time.Millisecond
			ctx, cancel := context.WithTimeout(context.Background(), jobTimeout)
			resp, err := c.SubmitJob(ctx, &coordinatorv1.SubmitJobRequest{
				JobName:   fmt.Sprintf("%s-layer-%s", animSceneName, layerStem),
				NumFrames: nFrames,
				Width:     animWidth,
				Height:    animHeight,
				JobType: &coordinatorv1.SubmitJobRequest_RenderJob{
					RenderJob: &coordinatorv1.RenderJob{
						SceneUri:        sceneURI,
						MaxSamples:      animMaxSamples,
						OutputUriPrefix: layerOutputPrefix,
						EnableDeep:      true,
						Threads:         animThreads,
						NoiseThreshold:  animNoiseThresh,
						MinSamples:      animMinSamples,
						AdaptiveStep:    animAdaptiveStep,
						LayerIndex:      int32(li),
						LayerName:       layerStem,
					},
				},
			})
			cancel()
			if err != nil {
				log.Fatalf("Failed to submit render job for layer %s: %v", layerStem, err)
			}
			fmt.Printf("[CLI] Submitted render job for layer %q (static=%v, frames=%d): %s\n",
				layerStem, isStatic[li], nFrames, resp.GetJobId())
			renderJobIDs[li] = resp.GetJobId()
		}

		// 9. Submit composite job depending on all render jobs.
		finalOutputPrefix := outputPrefix + "final/"
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		compResp, err := c.SubmitJob(ctx, &coordinatorv1.SubmitJobRequest{
			JobName:   animSceneName + "-composite",
			NumFrames: int32(numFrames),
			Width:     animWidth,
			Height:    animHeight,
			DependsOn: renderJobIDs,
			JobType: &coordinatorv1.SubmitJobRequest_CompositeJob{
				CompositeJob: &coordinatorv1.CompositeJob{
					LayerUriPrefixes:   layerOutputPrefixes,
					OutputUriPrefix:    finalOutputPrefix,
					StaticLayerIndices: staticLayerIndices,
				},
			},
		})
		cancel()
		if err != nil {
			log.Fatalf("Failed to submit composite job: %v", err)
		}
		fmt.Printf("[CLI] Submitted composite job: %s\n", compResp.GetJobId())
		fmt.Printf("\n[CLI] Animation %q submitted (%d layers, %d frames).\n",
			animSceneName, numLayers, numFrames)
		fmt.Printf("[CLI] Final output: %sframe-0001.exr … frame-%04d.exr\n",
			finalOutputPrefix, numFrames)
	},
}

// layerStemFromName strips the directory prefix and extension from a layer filename,
// then strips any trailing _NNNN frame number to give the base stem.
// e.g. "layer_dodecahedron_0042.json" → "layer_dodecahedron"
//
//	"layer_environment.json"      → "layer_environment"
func layerStemFromName(name string) string {
	base := strings.TrimSuffix(filepath.Base(name), filepath.Ext(name))
	// Strip trailing _NNNN (four digit frame number)
	re := regexp.MustCompile(`_\d{4}$`)
	return re.ReplaceAllString(base, "")
}

// uploadDirToGCS recursively uploads all files under localDir to gcsPrefix.
func uploadDirToGCS(localDir, gcsPrefix string, count *int) error {
	return filepath.WalkDir(localDir, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		rel, err := filepath.Rel(localDir, path)
		if err != nil {
			return err
		}
		// Construct the GCS path preserving subdirectory structure.
		targetPrefix := strings.TrimSuffix(gcsPrefix, "/") + "/" + filepath.Dir(rel)
		if filepath.Dir(rel) == "." {
			targetPrefix = strings.TrimSuffix(gcsPrefix, "/")
		}
		if _, err := uploadFileToGCS(path, targetPrefix); err != nil {
			return fmt.Errorf("uploading %s: %w", rel, err)
		}
		*count++
		return nil
	})
}

func init() {
	rootCmd.AddCommand(submitAnimationCmd)

	submitAnimationCmd.Flags().StringVar(&animSceneDir, "scene-dir", "", "Path to local animation scene directory (contains scene_NNNN.json files)")
	submitAnimationCmd.Flags().StringVar(&animUploadPrefix, "upload", "", "GCS prefix to upload scene files to (e.g. gs://bucket/scenes/my-animation/)")
	submitAnimationCmd.Flags().StringVar(&animOutputBucket, "bucket", "", "GCS bucket root for render outputs (e.g. gs://my-bucket)")
	submitAnimationCmd.Flags().StringVar(&animSceneName, "name", "", "Animation name used in output paths (default: scene-dir base name)")
	submitAnimationCmd.Flags().Int32VarP(&animMaxSamples, "samples", "S", 0, "Max samples per pixel (0 = use scene JSON)")
	submitAnimationCmd.Flags().Int32VarP(&animWidth, "width", "W", 0, "Image width (0 = use scene JSON)")
	submitAnimationCmd.Flags().Int32VarP(&animHeight, "height", "H", 0, "Image height (0 = use scene JSON)")
	submitAnimationCmd.Flags().Int32VarP(&animThreads, "threads", "t", 0, "Threads per worker (0 = auto)")
	submitAnimationCmd.Flags().Float32Var(&animNoiseThresh, "noise-threshold", 0.0, "Adaptive sampling noise threshold")
	submitAnimationCmd.Flags().Int32Var(&animMinSamples, "min-samples", 0, "Minimum samples before adaptive check")
	submitAnimationCmd.Flags().Int32Var(&animAdaptiveStep, "adaptive-step", 0, "Samples between adaptive convergence checks")
	submitAnimationCmd.Flags().BoolVar(&animSkipUpload, "skip-upload", false, "Skip uploading scene files (use when already on GCS)")

	for _, f := range []string{"scene-dir", "upload", "bucket"} {
		if err := submitAnimationCmd.MarkFlagRequired(f); err != nil {
			log.Fatalf("Failed to mark %q flag as required: %v", f, err)
		}
	}
}

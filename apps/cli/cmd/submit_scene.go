package cmd

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	coordinatorv1 "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	sceneSceneFile     string
	sceneUploadPrefix  string
	sceneOutputBucket  string
	sceneSceneName     string
	sceneMaxSamples    int32
	sceneWidth         int32
	sceneHeight        int32
	sceneThreads       int32
	sceneNoiseThresh   float32
	sceneMinSamples    int32
	sceneAdaptiveStep  int32
)

// sceneConfig is the minimal JSON structure we need to parse from a scene.json.
type sceneConfig struct {
	Layers []string `json:"layers"`
}

var submitSceneCmd = &cobra.Command{
	Use:   "submit-scene",
	Short: "Submit a multi-layer scene: one Cloud Run job per layer + a composite job",
	Long: `Reads a scene JSON file, discovers its layers, uploads all scene files to GCS,
then submits one RenderJob per layer followed by a CompositeJob that depends on all layers.

Output is written to:
  gs://<bucket>/renders/<scene-name>/<layer-stem>.exr   (per-layer deep EXRs)
  gs://<bucket>/renders/<scene-name>/final/frame-0001.exr  (composited result)
`,
	Run: func(cmd *cobra.Command, args []string) {
		// 1. Read and parse the scene JSON locally.
		sceneData, err := os.ReadFile(sceneSceneFile)
		if err != nil {
			log.Fatalf("Failed to read scene file: %v", err)
		}
		var cfg sceneConfig
		if err := json.Unmarshal(sceneData, &cfg); err != nil {
			log.Fatalf("Failed to parse scene JSON: %v", err)
		}
		if len(cfg.Layers) == 0 {
			log.Fatalf("Scene file has no layers: %s", sceneSceneFile)
		}

		// 2. Derive scene name from directory or flag.
		if sceneSceneName == "" {
			sceneSceneName = filepath.Base(filepath.Dir(sceneSceneFile))
		}

		// 3. Upload the whole scene directory to GCS.
		if !strings.HasPrefix(sceneUploadPrefix, "gs://") {
			log.Fatalf("--upload must be a gs:// URI, got: %s", sceneUploadPrefix)
		}
		if !strings.HasPrefix(sceneOutputBucket, "gs://") {
			log.Fatalf("--bucket must be a gs:// URI, got: %s", sceneOutputBucket)
		}

		sceneDir := filepath.Dir(sceneSceneFile)
		gcsSceneURI, err := uploadFileToGCS(sceneSceneFile, sceneUploadPrefix)
		if err != nil {
			log.Fatalf("Failed to upload scene file: %v", err)
		}
		fmt.Printf("[CLI] Uploaded scene: %s\n", gcsSceneURI)

		// Upload every layer file referenced in the scene.
		for _, layerFile := range cfg.Layers {
			localPath := filepath.Join(sceneDir, layerFile)
			gcsPath, err := uploadFileToGCS(localPath, sceneUploadPrefix)
			if err != nil {
				log.Fatalf("Failed to upload layer %s: %v", layerFile, err)
			}
			fmt.Printf("[CLI] Uploaded layer: %s\n", gcsPath)
		}

		// Also upload any other JSON files (context files) in the scene dir.
		entries, err := os.ReadDir(sceneDir)
		if err != nil {
			log.Fatalf("Failed to read scene directory: %v", err)
		}
		for _, entry := range entries {
			if entry.IsDir() || filepath.Ext(entry.Name()) != ".json" {
				continue
			}
			if entry.Name() == filepath.Base(sceneSceneFile) {
				continue // already uploaded
			}
			alreadyUploaded := false
			for _, lf := range cfg.Layers {
				if filepath.Base(lf) == entry.Name() {
					alreadyUploaded = true
					break
				}
			}
			if alreadyUploaded {
				continue
			}
			gcsPath, err := uploadFileToGCS(filepath.Join(sceneDir, entry.Name()), sceneUploadPrefix)
			if err != nil {
				log.Printf("[CLI] Warning: failed to upload %s: %v", entry.Name(), err)
				continue
			}
			fmt.Printf("[CLI] Uploaded context: %s\n", gcsPath)
		}

		// 4. Connect to coordinator.
		if err := ensureConnection(coordinatorAddr); err != nil {
			log.Fatalf("Error establishing connection: %v", err)
		}
		conn, err := grpc.NewClient(coordinatorAddr, grpc.WithTransportCredentials(insecure.NewCredentials()))
		if err != nil {
			log.Fatalf("Did not connect: %v", err)
		}
		defer conn.Close()
		c := coordinatorv1.NewCoordinatorServiceClient(conn)

		outputPrefix := strings.TrimSuffix(sceneOutputBucket, "/") + "/renders/" + sceneSceneName + "/"

		// 5. Submit one RenderJob per layer.
		layerJobIDs := make([]string, 0, len(cfg.Layers))
		layerOutputPrefixes := make([]string, 0, len(cfg.Layers))

		for i, layerFile := range cfg.Layers {
			layerStem := strings.TrimSuffix(filepath.Base(layerFile), filepath.Ext(layerFile))
			// Each layer gets its own subfolder so the composite job can locate
			// its frame-0001.exr via the per-layer prefix.
			layerOutputPrefix := outputPrefix + layerStem + "/"

			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			resp, err := c.SubmitJob(ctx, &coordinatorv1.SubmitJobRequest{
				JobName:   fmt.Sprintf("%s-%s", sceneSceneName, layerStem),
				NumFrames: 1,
				Width:     sceneWidth,
				Height:    sceneHeight,
				JobType: &coordinatorv1.SubmitJobRequest_RenderJob{
					RenderJob: &coordinatorv1.RenderJob{
						SceneUri:        gcsSceneURI,
						MaxSamples:      sceneMaxSamples,
						OutputUriPrefix: layerOutputPrefix,
						EnableDeep:      true,
						Threads:         sceneThreads,
						NoiseThreshold:  sceneNoiseThresh,
						MinSamples:      sceneMinSamples,
						AdaptiveStep:    sceneAdaptiveStep,
						LayerIndex:      int32(i),
						LayerName:       layerStem,
					},
				},
			})
			cancel()
			if err != nil {
				log.Fatalf("Failed to submit render job for layer %s: %v", layerStem, err)
			}
			fmt.Printf("[CLI] Submitted render job for layer %q: %s\n", layerStem, resp.GetJobId())
			layerJobIDs = append(layerJobIDs, resp.GetJobId())
			layerOutputPrefixes = append(layerOutputPrefixes, layerOutputPrefix)
		}

		// 6. Submit CompositeJob that depends on all render jobs.
		finalOutputPrefix := outputPrefix + "final/"
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		compResp, err := c.SubmitJob(ctx, &coordinatorv1.SubmitJobRequest{
			JobName:   sceneSceneName + "-composite",
			NumFrames: 1,
			Width:     sceneWidth,
			Height:    sceneHeight,
			DependsOn: layerJobIDs,
			JobType: &coordinatorv1.SubmitJobRequest_CompositeJob{
				CompositeJob: &coordinatorv1.CompositeJob{
					LayerUriPrefixes: layerOutputPrefixes,
					OutputUriPrefix:  finalOutputPrefix,
				},
			},
		})
		cancel()
		if err != nil {
			log.Fatalf("Failed to submit composite job: %v", err)
		}
		fmt.Printf("[CLI] Submitted composite job: %s\n", compResp.GetJobId())
		fmt.Printf("\n[CLI] Scene %q submitted (%d layers).\n", sceneSceneName, len(cfg.Layers))
		fmt.Printf("[CLI] Final output: %sframe-0001.exr\n", finalOutputPrefix)
	},
}


func init() {
	rootCmd.AddCommand(submitSceneCmd)

	submitSceneCmd.Flags().StringVarP(&sceneSceneFile, "scene", "s", "", "Path to local scene.json file")
	submitSceneCmd.Flags().StringVar(&sceneUploadPrefix, "upload", "", "GCS prefix to upload scene files to (e.g. gs://bucket/scenes/my-scene/)")
	submitSceneCmd.Flags().StringVar(&sceneOutputBucket, "bucket", "", "GCS bucket root for render outputs (e.g. gs://my-bucket)")
	submitSceneCmd.Flags().StringVar(&sceneSceneName, "name", "", "Scene name used in output paths (default: parent directory name)")
	submitSceneCmd.Flags().Int32VarP(&sceneMaxSamples, "samples", "S", 0, "Max samples per pixel (0 = use scene JSON)")
	submitSceneCmd.Flags().Int32VarP(&sceneWidth, "width", "W", 0, "Image width (0 = use scene JSON)")
	submitSceneCmd.Flags().Int32VarP(&sceneHeight, "height", "H", 0, "Image height (0 = use scene JSON)")
	submitSceneCmd.Flags().Int32VarP(&sceneThreads, "threads", "t", 0, "Threads per worker (0 = auto)")
	submitSceneCmd.Flags().Float32Var(&sceneNoiseThresh, "noise-threshold", 0.0, "Adaptive sampling noise threshold")
	submitSceneCmd.Flags().Int32Var(&sceneMinSamples, "min-samples", 0, "Minimum samples before adaptive check")
	submitSceneCmd.Flags().Int32Var(&sceneAdaptiveStep, "adaptive-step", 0, "Samples between adaptive convergence checks")

	for _, f := range []string{"scene", "upload", "bucket"} {
		if err := submitSceneCmd.MarkFlagRequired(f); err != nil {
			log.Fatalf("Failed to mark %q flag as required: %v", f, err)
		}
	}
}

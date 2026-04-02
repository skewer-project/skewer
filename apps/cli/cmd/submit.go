package cmd

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"cloud.google.com/go/storage"
	coordinatorv1 "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	sceneURI       string
	jobName        string
	outputURI      string
	uploadPrefix   string
	numFrames      int32
	width          int32
	height         int32
	maxSamples     int32
	enableDeep     bool
	threads        int32
	noiseThreshold float32
	minSamples     int32
	adaptiveStep   int32
)

var submitCmd = &cobra.Command{
	Use:   "submit",
	Short: "Submit a rendering job to the Skewer coordinator",
	Run: func(cmd *cobra.Command, args []string) {
		// Upload local scene file(s) to GCS if --upload is provided
		if uploadPrefix != "" {
			if !strings.HasPrefix(uploadPrefix, "gs://") {
				log.Fatalf("--upload must be a gs:// URI, got: %s", uploadPrefix)
			}
			if strings.Contains(sceneURI, "####") {
				for i := int32(1); i <= numFrames; i++ {
					framePath := strings.ReplaceAll(sceneURI, "####", fmt.Sprintf("%04d", i))
					gcsPath, err := uploadFileToGCS(framePath, uploadPrefix)
					if err != nil {
						log.Fatalf("Failed to upload scene for frame %d: %v", i, err)
					}
					fmt.Printf("[CLI] Uploaded frame %d scene: %s\n", i, gcsPath)
					if i == 1 {
						// Derive the GCS pattern from the first uploaded path
						sceneURI = strings.ReplaceAll(gcsPath, fmt.Sprintf("%04d", i), "####")
					}
				}
			} else {
				gcsPath, err := uploadFileToGCS(sceneURI, uploadPrefix)
				if err != nil {
					log.Fatalf("Failed to upload scene: %v", err)
				}
				sceneURI = gcsPath
				fmt.Printf("[CLI] Uploaded scene: %s\n", sceneURI)
			}
		}

		// Validate gs:// URIs — required for GCS-backed submissions
		if strings.HasPrefix(sceneURI, "gs://") || uploadPrefix != "" {
			if !strings.HasPrefix(sceneURI, "gs://") {
				log.Fatalf("--scene must be a gs:// URI when submitting to cloud. Use --upload to upload local files first.")
			}
			if !strings.HasPrefix(outputURI, "gs://") {
				log.Fatalf("--output must be a gs:// URI when --scene is a gs:// URI, got: %s", outputURI)
			}
		} else {
			// Local mode: validate scene file(s) exist
			if strings.Contains(sceneURI, "####") {
				for i := int32(1); i <= numFrames; i++ {
					framePath := strings.ReplaceAll(sceneURI, "####", fmt.Sprintf("%04d", i))
					if _, err := os.Stat(framePath); os.IsNotExist(err) {
						log.Fatalf("Scene file for frame %d not found: %s", i, framePath)
					}
				}
			} else {
				if _, err := os.Stat(sceneURI); os.IsNotExist(err) {
					log.Fatalf("Scene file not found: %s", sceneURI)
				}
			}
		}

		// Automate port-forwarding if needed
		if err := ensureConnection(coordinatorAddr); err != nil {
			log.Fatalf("Error establishing connection: %v", err)
		}

		conn, err := grpc.NewClient(coordinatorAddr, grpc.WithTransportCredentials(insecure.NewCredentials()))
		if err != nil {
			log.Fatalf("Did not connect: %v", err)
		}
		defer conn.Close()
		c := coordinatorv1.NewCoordinatorServiceClient(conn)

		ctx, cancel := context.WithTimeout(context.Background(), time.Second*5)
		defer cancel()

		req := &coordinatorv1.SubmitJobRequest{
			JobName:   jobName,
			NumFrames: numFrames,
			Width:     width,
			Height:    height,
			JobType: &coordinatorv1.SubmitJobRequest_RenderJob{
				RenderJob: &coordinatorv1.RenderJob{
					SceneUri:        sceneURI,
					MaxSamples:      maxSamples,
					OutputUriPrefix: outputURI,
					EnableDeep:      enableDeep,
					Threads:         threads,
					NoiseThreshold:  noiseThreshold,
					MinSamples:      minSamples,
					AdaptiveStep:    adaptiveStep,
				},
			},
		}

		r, err := c.SubmitJob(ctx, req)
		if err != nil {
			log.Fatalf("Could not submit job: %v", err)
		}
		fmt.Printf("Job submitted successfully! Job ID: %s\n", r.GetJobId())
	},
}

// uploadFileToGCS uploads a local file to the given GCS prefix and returns the gs:// URI.
func uploadFileToGCS(localPath, gcsPrefix string) (string, error) {
	ctx := context.Background()
	client, err := storage.NewClient(ctx)
	if err != nil {
		return "", fmt.Errorf("creating GCS client: %w", err)
	}
	defer client.Close()

	// Parse gs://bucket/prefix
	trimmed := strings.TrimPrefix(gcsPrefix, "gs://")
	sep := strings.IndexByte(trimmed, '/')
	var bucket, objPrefix string
	if sep < 0 {
		bucket = trimmed
	} else {
		bucket = trimmed[:sep]
		objPrefix = strings.TrimSuffix(trimmed[sep+1:], "/")
	}

	filename := filepath.Base(localPath)
	var objName string
	if objPrefix != "" {
		objName = objPrefix + "/" + filename
	} else {
		objName = filename
	}

	f, err := os.Open(localPath)
	if err != nil {
		return "", fmt.Errorf("opening %s: %w", localPath, err)
	}
	defer f.Close()

	wc := client.Bucket(bucket).Object(objName).NewWriter(ctx)
	if _, err := io.Copy(wc, f); err != nil {
		wc.Close()
		return "", fmt.Errorf("uploading %s to gs://%s/%s: %w", localPath, bucket, objName, err)
	}
	if err := wc.Close(); err != nil {
		return "", fmt.Errorf("finalizing upload of %s: %w", objName, err)
	}

	return "gs://" + bucket + "/" + objName, nil
}

func init() {
	rootCmd.AddCommand(submitCmd)

	submitCmd.Flags().StringVarP(&sceneURI, "scene", "s", "", "URI of the scene file to render (local path or gs://)")
	submitCmd.Flags().StringVarP(&jobName, "name", "n", "skewer-job", "Name of the job")
	submitCmd.Flags().StringVarP(&outputURI, "output", "o", "data/renders/", "Output URI prefix (local path or gs://)")
	submitCmd.Flags().StringVar(&uploadPrefix, "upload", "", "Upload local scene file(s) to this GCS prefix before submitting (e.g. gs://my-bucket/scenes/)")
	submitCmd.Flags().Int32VarP(&numFrames, "frames", "f", 1, "Number of frames to render")
	submitCmd.Flags().Int32VarP(&width, "width", "W", 0, "Width of the rendered image (0 = use scene JSON)")
	submitCmd.Flags().Int32VarP(&height, "height", "H", 0, "Height of the rendered image (0 = use scene JSON)")
	submitCmd.Flags().Int32VarP(&maxSamples, "samples", "S", 0, "Total samples per pixel (0 = use scene JSON)")
	submitCmd.Flags().BoolVar(&enableDeep, "deep", false, "Enable deep output for the final image")
	submitCmd.Flags().Int32VarP(&threads, "threads", "t", 0, "Number of threads per worker (0 = auto)")
	submitCmd.Flags().Float32Var(&noiseThreshold, "noise-threshold", 0.0, "Adaptive sampling noise threshold (0 = use scene JSON setting)")
	submitCmd.Flags().Int32Var(&minSamples, "min-samples", 0, "Minimum samples before adaptive convergence check (0 = use scene JSON setting)")
	submitCmd.Flags().Int32Var(&adaptiveStep, "adaptive-step", 0, "Samples between adaptive convergence checks (0 = use scene JSON setting)")

	if err := submitCmd.MarkFlagRequired("scene"); err != nil {
		log.Fatalf("Failed to mark 'scene' flag as required: %v", err)
	}
}

package cli

import (
	"context"
	"fmt"
	"log"
	"os"
	"strings"
	"time"

	coordinatorv1 "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	sceneURI       string
	jobName        string
	outputURI      string
	numFrames      int32
	width          int32
	height         int32
	maxSamples     int32
	enableDeep     bool
	threads        int32
	noiseThreshold float32
	minSamples     int32
	adaptiveStep   int32
	dependsOn      []string
)

var renderCmd = &cobra.Command{
	Use:   "render",
	Short: "Submit a rendering job to the Skewer coordinator",
	Run: func(cmd *cobra.Command, args []string) {
		// Validation: Check if local scene files exist if '####' is present
		if !strings.HasPrefix(sceneURI, "gs://") && strings.Contains(sceneURI, "####") {
			for i := int32(1); i <= numFrames; i++ {
				framePath := strings.ReplaceAll(sceneURI, "####", fmt.Sprintf("%04d", i))
				if _, err := os.Stat(framePath); os.IsNotExist(err) {
					log.Fatalf("Scene file for frame %d not found: %s", i, framePath)
				}
			}
		} else if !strings.HasPrefix(sceneURI, "gs://") {
			if _, err := os.Stat(sceneURI); os.IsNotExist(err) {
				log.Fatalf("Scene file not found: %s", sceneURI)
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
			DependsOn: dependsOn,
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
			log.Fatalf("Could not submit renderjob: %v", err)
		}
		fmt.Printf("Job submitted successfully! Job ID: %s\n", r.GetJobId())
	},
}

func init() {
	rootCmd.AddCommand(renderCmd)

	renderCmd.Flags().StringVarP(&sceneURI, "scene", "s", "", "URI of the scene file to render (can be local path or gs://)")
	renderCmd.Flags().StringVarP(&jobName, "name", "n", "skewer-job", "Name of the job")
	renderCmd.Flags().StringVarP(&outputURI, "output", "o", "data/renders/", "Output URI prefix (can be local path or gs://)")
	renderCmd.Flags().Int32VarP(&numFrames, "frames", "f", 1, "Number of frames to render")
	renderCmd.Flags().Int32VarP(&width, "width", "W", 0, "Width of the rendered image (0 = use scene JSON)")
	renderCmd.Flags().Int32VarP(&height, "height", "H", 0, "Height of the rendered image (0 = use scene JSON)")
	renderCmd.Flags().Int32VarP(&maxSamples, "samples", "S", 0, "Total samples per pixel (0 = use scene JSON)")
	renderCmd.Flags().BoolVar(&enableDeep, "deep", false, "Enable deep output for the final image")
	renderCmd.Flags().Int32VarP(&threads, "threads", "t", 0, "Number of threads per worker (0 = auto)")
	renderCmd.Flags().Float32Var(&noiseThreshold, "noise-threshold", 0.0, "Adaptive sampling noise threshold (0 = use scene JSON setting)")
	renderCmd.Flags().Int32Var(&minSamples, "min-samples", 0, "Minimum samples before adaptive convergence check (0 = use scene JSON setting)")
	renderCmd.Flags().Int32Var(&adaptiveStep, "adaptive-step", 0, "Samples between adaptive convergence checks (0 = use scene JSON setting)")
	renderCmd.Flags().StringSliceVarP(&dependsOn, "depends-on", "d", []string{}, "Comma-separated list of Job IDs this job depends on")

	if err := renderCmd.MarkFlagRequired("scene"); err != nil {
		log.Fatalf("Failed to mark 'scene' flag as required: %v", err)
	}
}

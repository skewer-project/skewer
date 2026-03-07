package cmd

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
	numFrames      int
	width          int
	height         int
	totalSamples   int
	sampleDivision int
	enableDeep     bool
	threads        int
)

var submitCmd = &cobra.Command{
	Use:   "submit",
	Short: "Submit a rendering job to the Skewer coordinator",
	Run: func(cmd *cobra.Command, args []string) {
		// Validation: Check if local scene files exist if '####' is present
		if !strings.HasPrefix(sceneURI, "gs://") && strings.Contains(sceneURI, "####") {
			for i := 1; i <= numFrames; i++ {
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
			JobId:     "job-" + time.Now().Format("20060102150405"),
			JobName:   jobName,
			NumFrames: int32(numFrames),
			Width:     int32(width),
			Height:    int32(height),
			JobType: &coordinatorv1.SubmitJobRequest_RenderJob{
				RenderJob: &coordinatorv1.RenderJob{
					SceneUri:        sceneURI,
					TotalSamples:    int32(totalSamples),
					SampleDivision:  int32(sampleDivision),
					OutputUriPrefix: outputURI,
					EnableDeep:      enableDeep,
					Threads:         int32(threads),
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

func init() {
	rootCmd.AddCommand(submitCmd)

	submitCmd.Flags().StringVarP(&sceneURI, "scene", "s", "", "URI of the scene file to render (can be local path or gs://)")
	submitCmd.Flags().StringVarP(&jobName, "name", "n", "skewer-job", "Name of the job")
	submitCmd.Flags().StringVarP(&outputURI, "output", "o", "/data/renders/", "Output URI prefix (can be local path or gs://)")
	submitCmd.Flags().IntVarP(&numFrames, "frames", "f", 1, "Number of frames to render")
	submitCmd.Flags().IntVarP(&width, "width", "W", 1920, "Width of the rendered image")
	submitCmd.Flags().IntVarP(&height, "height", "H", 1080, "Height of the rendered image")
	submitCmd.Flags().IntVarP(&totalSamples, "samples", "S", 1024, "Total samples per pixel")
	submitCmd.Flags().IntVarP(&sampleDivision, "division", "d", 4, "Number of tasks to split each frame into")
	submitCmd.Flags().BoolVar(&enableDeep, "deep", false, "Enable deep output for the final image")
	submitCmd.Flags().IntVarP(&threads, "threads", "t", 0, "Number of threads per worker (0 = auto)")

	submitCmd.MarkFlagRequired("scene")
}

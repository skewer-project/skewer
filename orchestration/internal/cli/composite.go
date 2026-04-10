package cli

import (
	"context"
	"fmt"
	"log"
	"time"

	coordinatorv1 "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

var (
	compJobName   string
	compOutputURI string
	compNumFrames int32
	compWidth     int32
	compHeight    int32
	compDependsOn []string
	compLayers    []string
)

var compositeCmd = &cobra.Command{
	Use:   "composite",
	Short: "Submit a compositing job to the Skewer coordinator",
	Long: `Submit a compositing job. If --layers is omitted, the coordinator will
automatically resolve input layers from the jobs listed in --depends-on.`,
	Run: func(cmd *cobra.Command, args []string) {
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
			JobName:   compJobName,
			NumFrames: compNumFrames,
			Width:     compWidth,
			Height:    compHeight,
			DependsOn: compDependsOn,
			JobType: &coordinatorv1.SubmitJobRequest_CompositeJob{
				CompositeJob: &coordinatorv1.CompositeJob{
					LayerUriPrefixes: compLayers,
					OutputUriPrefix:  compOutputURI,
				},
			},
		}

		r, err := c.SubmitJob(ctx, req)
		if err != nil {
			log.Fatalf("Could not submit composite job: %v", err)
		}
		fmt.Printf("Composite job submitted successfully! Job ID: %s\n", r.GetJobId())
	},
}

func init() {
	rootCmd.AddCommand(compositeCmd)

	compositeCmd.Flags().StringVarP(&compJobName, "name", "n", "skewer-composite", "Name of the job")
	compositeCmd.Flags().StringVarP(&compOutputURI, "output", "o", "data/renders/", "Output URI prefix (can be local path or gs://)")
	compositeCmd.Flags().Int32VarP(&compNumFrames, "frames", "f", 1, "Number of frames to composite")
	compositeCmd.Flags().Int32VarP(&compWidth, "width", "W", 0, "Width of the output image (0 = use inputs)")
	compositeCmd.Flags().Int32VarP(&compHeight, "height", "H", 0, "Height of the output image (0 = use inputs)")
	compositeCmd.Flags().StringSliceVarP(&compDependsOn, "depends-on", "d", []string{}, "Comma-separated list of Job IDs this job depends on")
	compositeCmd.Flags().StringSliceVarP(&compLayers, "layers", "l", []string{}, "Optional: Manual comma-separated list of layer URI prefixes")
}

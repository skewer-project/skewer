package cmd

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
	coordinatorAddr string
	sceneURI        string
	jobName         string
)

var submitCmd = &cobra.Command{
	Use:   "submit",
	Short: "Submit a rendering job to the Skewer coordinator",
	Run: func(cmd *cobra.Command, args []string) {
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
			NumFrames: 3,
			JobType: &coordinatorv1.SubmitJobRequest_RenderJob{
				RenderJob: &coordinatorv1.RenderJob{
					SceneUri:        sceneURI,
					TotalSamples:    1024,
					SampleDivision:  4,
					OutputUriPrefix: "gs://skewer-renders/" + jobName + "/",
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

	submitCmd.Flags().StringVarP(&coordinatorAddr, "coordinator", "c", "localhost:50051", "Address of the Skewer Coordinator")
	submitCmd.Flags().StringVarP(&sceneURI, "scene", "s", "", "URI of the scene file to render (e.g., gs://bucket/scene.json)")
	submitCmd.Flags().StringVarP(&jobName, "name", "n", "skewer-job", "Name of the job")
	submitCmd.MarkFlagRequired("scene")
}

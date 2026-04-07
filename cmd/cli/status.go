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

var statusJobID string

var statusCmd = &cobra.Command{
	Use:   "status",
	Short: "Get the status of a submitted job",
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

		req := &coordinatorv1.GetJobStatusRequest{
			JobId: statusJobID,
		}

		r, err := c.GetJobStatus(ctx, req)
		if err != nil {
			log.Fatalf("Could not get status: %v", err)
		}

		fmt.Printf("Job Status: %s\n", r.GetJobStatus().String())
		fmt.Printf("Progress: %.1f%%\n", r.GetProgressPercent())
		if r.GetErrorMessage() != "" {
			fmt.Printf("Error: %s\n", r.GetErrorMessage())
		}
	},
}

func init() {
	rootCmd.AddCommand(statusCmd)
	statusCmd.Flags().StringVarP(&statusJobID, "job", "j", "", "The UUID of the job to check")

	if err := statusCmd.MarkFlagRequired("job"); err != nil {
		log.Fatalf("Failed to mark 'job' flag as required: %v", err)
	}
}

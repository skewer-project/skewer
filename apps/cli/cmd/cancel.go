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

var cancelJobID string

var cancelCmd = &cobra.Command{
	Use:   "cancel",
	Short: "Cancel a running Skewer job",
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

		req := &coordinatorv1.CancelJobRequest{
			JobId: cancelJobID,
		}

		r, err := c.CancelJob(ctx, req)
		if err != nil {
			log.Fatalf("Could not cancel job: %v", err)
		}

		if r.GetSuccess() {
			fmt.Println("Job cancellation signal sent successfully.")
		} else {
			fmt.Println("Failed to cancel job (it may have already completed or failed).")
		}
	},
}

func init() {
	rootCmd.AddCommand(cancelCmd)
	cancelCmd.Flags().StringVarP(&cancelJobID, "job", "j", "", "The UUID of the job to cancel")

	if err := cancelCmd.MarkFlagRequired("job"); err != nil {
		log.Fatalf("Failed to mark 'job' flag as required: %v", err)
	}
}

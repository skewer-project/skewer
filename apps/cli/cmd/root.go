package cmd

import (
	"os"

	"github.com/spf13/cobra"
)

var coordinatorAddr string
var useTLS bool

var rootCmd = &cobra.Command{
	Use:   "skewer",
	Short: "Skewer is a distributed rendering orchestrator.",
	Long: `Skewer CLI submits and manages rendering pipelines
for the Skewer distributed rendering engine on GCP.`,
}

// Execute adds all child commands to the root command and sets flags appropriately.
func Execute() {
	err := rootCmd.Execute()
	if err != nil {
		os.Exit(1)
	}
}

func init() {
	rootCmd.PersistentFlags().StringVarP(&coordinatorAddr, "coordinator", "c", "localhost:50051",
		"Address of the Skewer Coordinator (e.g. skewer-coordinator-xyz.run.app:443)")
	rootCmd.PersistentFlags().BoolVar(&useTLS, "tls", false,
		"Use TLS when connecting to the coordinator (required for Cloud Run endpoints)")

	rootCmd.AddCommand(pipelineCmd)
}

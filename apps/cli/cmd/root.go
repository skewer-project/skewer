package cmd

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/spf13/cobra"
)

var coordinatorAddr string

func ensureConnection(addr string) error {
	// If not localhost, don't try to port-forward
	if !strings.HasPrefix(addr, "localhost:") && !strings.HasPrefix(addr, "127.0.0.1:") {
		return nil
	}

	_, err := net.DialTimeout("tcp", addr, 1*time.Second)
	if err == nil {
		// Connection already works
		return nil
	}

	fmt.Println("[CLI] Coordinator not reachable. Attempting to port-forward...")

	// Spawn kubectl port-forward in the background
	// We'll just run it and hope for the best, letting it stay alive
	cmd := exec.Command("kubectl", "port-forward", "svc/skewer-coordinator", "50051:50051")
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start kubectl port-forward: %w", err)
	}

	// Wait a bit for the connection to establish
	maxRetries := 5
	for i := 0; i < maxRetries; i++ {
		time.Sleep(1 * time.Second)
		_, err := net.DialTimeout("tcp", addr, 1*time.Second)
		if err == nil {
			fmt.Println("[CLI] Port-forwarding successful.")
			return nil
		}
	}

	return fmt.Errorf("failed to connect to coordinator after port-forwarding")
}

// rootCmd represents the base command when called without any subcommands
var rootCmd = &cobra.Command{
	Use:   "skewer",
	Short: "A brief description of your application",
	Long: `A longer description that spans multiple lines and likely contains
examples and usage of using your application. For example:

Cobra is a CLI library for Go that empowers applications.
This application is a tool to generate the needed files
to quickly create a Cobra application.`,
	// Uncomment the following line if your bare application
	// has an action associated with it:
	// Run: func(cmd *cobra.Command, args []string) { },
}

// Execute adds all child commands to the root command and sets flags appropriately.
// This is called by main.main(). It only needs to happen once to the rootCmd.
func Execute() {
	err := rootCmd.Execute()
	if err != nil {
		os.Exit(1)
	}
}

func init() {
	// Here you will define your flags and configuration settings.
	// Cobra supports persistent flags, which, if defined here,
	// will be global for your application.

	rootCmd.PersistentFlags().StringVarP(&coordinatorAddr, "coordinator", "c", "localhost:50051", "Address of the Skewer Coordinator")

	// Cobra also supports local flags, which will only run
	// when this action is called directly.
	rootCmd.Flags().BoolP("toggle", "t", false, "Help message for toggle")
}

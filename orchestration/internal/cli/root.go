package cli

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
var pfCmd *exec.Cmd // Store a reference to the port-forward command

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

	fmt.Println("[CLI]: Coordinator not reachable. Attempting to port-forward...")

	// Extract the port from the address (e.g. "localhost:50051" -> "50051")
	parts := strings.Split(addr, ":")
	port := parts[len(parts)-1]
	portArg := fmt.Sprintf("%s:%s", port, port)

	// Spawn kubectl port-forward in the background
	pfCmd = exec.Command("kubectl", "port-forward", "svc/skewer-coordinator", portArg)
	if err := pfCmd.Start(); err != nil {
		return fmt.Errorf("failed to start kubectl port-forward: %w", err)
	}

	// Wait a bit for the connection to establish
	maxRetries := 5
	for i := 0; i < maxRetries; i++ {
		time.Sleep(1 * time.Second)
		_, err := net.DialTimeout("tcp", addr, 1*time.Second)
		if err == nil {
			fmt.Println("[CLI]: Port-forwarding successful.")
			return nil
		}
	}

	return fmt.Errorf("failed to connect to coordinator after port-forwarding")
}

// Clean up port-forward process if we started one
func cleanupPortForward() {
	if pfCmd != nil && pfCmd.Process != nil {
		pfCmd.Process.Kill()
		pfCmd.Wait()
	}
}

// rootCmd represents the base command when called without any subcommands
var rootCmd = &cobra.Command{
	Use:   "skewer",
	Short: "Skewer is a distributed rendering orchestrator.",
	Long: `Skewer CLI submits and manages rendering and compositing jobs
   for the Skewer distributed rendering engine.`,
	PersistentPostRun: func(cmd *cobra.Command, args []string) {
		// This runs after every command completes
		cleanupPortForward()
	},
}

// Execute adds all child commands to the root command and sets flags appropriately.
// This is called by main.main(). It only needs to happen once to the rootCmd.
func Execute() {
	err := rootCmd.Execute()
	if err != nil {
		cleanupPortForward()
		os.Exit(1)
	}
}

func init() {
	// Here we define your flags and configuration settings.
	rootCmd.PersistentFlags().StringVarP(&coordinatorAddr, "coordinator", "c", "localhost:50051", "Address of the Skewer Coordinator")
}

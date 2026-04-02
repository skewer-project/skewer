package cmd

import (
	"fmt"
	"os"
	"os/exec"

	"github.com/spf13/cobra"
)

var (
	projectID   string
	region      string
	credsFile   string
	clusterName string
)

// cloudCmd represents the cloud command category
var cloudCmd = &cobra.Command{
	Use:   "cloud",
	Short: "Manage the Bring Your Own Render Farm cloud deployment",
	Long: `Cloud commands allow you to automatically provision a Google Kubernetes Engine (GKE) 
cluster, install KEDA autoscalers, securely upload your GCP service account credentials, and teardown the cluster.`,
}

// provisionCmd automates the creation of a GKE cluster via gcloud
var provisionCmd = &cobra.Command{
	Use:   "provision",
	Short: "Provision a new GKE auto-scaling cluster",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Printf("[CLI]: Provisioning GKE cluster '%s' in %s...\n", clusterName, region)

		// Wrapping gcloud commands for IaC convenience
		gcloudArgs := []string{
			"container", "clusters", "create", clusterName,
			"--project", projectID,
			"--region", region,
			"--num-nodes", "1",
			"--enable-autoscaling",
			"--min-nodes", "0",
			"--max-nodes", "10",
			"--machine-type", "e2-standard-4",
		}

		c := exec.Command("gcloud", gcloudArgs...)
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr

		if err := c.Run(); err != nil {
			fmt.Printf("[ERROR]: Failed to provision cluster: %v\n", err)
			os.Exit(1)
		}

		fmt.Println("[CLI]: Successfully provisioned GKE cluster! You can now run 'skewer cloud setup' to install Skewer.")
	},
}

// setupCmd configures the active cluster with KEDA and the user's GCP credentials
var setupCmd = &cobra.Command{
	Use:   "setup",
	Short: "Setup KEDA and GCP credentials in the active K8s cluster",
	Run: func(cmd *cobra.Command, args []string) {
		if credsFile == "" {
			fmt.Println("[ERROR]: You must provide a GCP Service Account JSON using --creds")
			os.Exit(1)
		}

		if _, err := os.Stat(credsFile); os.IsNotExist(err) {
			fmt.Printf("[ERROR]: Credentials file not found at %s\n", credsFile)
			os.Exit(1)
		}

		fmt.Println("[CLI]: 1. Installing KEDA (Kubernetes Event-driven Autoscaling)...")
		kedaCmd := exec.Command("kubectl", "apply", "--server-side", "-f", "https://github.com/kedacore/keda/releases/download/v2.13.0/keda-2.13.0.yaml")
		kedaCmd.Stdout = os.Stdout
		kedaCmd.Stderr = os.Stderr
		if err := kedaCmd.Run(); err != nil {
			fmt.Printf("[ERROR]: Failed to install KEDA: %v\n", err)
		}

		fmt.Println("\n[CLI]: 2. Uploading GCP Credentials to cluster Secret (skewer-gcp-creds)...")
		// Delete existing secret just in case
		exec.Command("kubectl", "delete", "secret", "skewer-gcp-creds", "--ignore-not-found").Run()

		secretCmd := exec.Command("kubectl", "create", "secret", "generic", "skewer-gcp-creds", fmt.Sprintf("--from-file=credentials.json=%s", credsFile))
		secretCmd.Stdout = os.Stdout
		secretCmd.Stderr = os.Stderr
		if err := secretCmd.Run(); err != nil {
			fmt.Printf("[ERROR]: Failed to create Secret: %v\n", err)
			os.Exit(1)
		}

		fmt.Println("\n[CLI]: 3. Applying KEDA ScaledObjects...")
		soCmd := exec.Command("kubectl", "apply", "-f", "deployments/k8s/keda.yaml")
		soCmd.Stdout = os.Stdout
		soCmd.Stderr = os.Stderr
		if err := soCmd.Run(); err != nil {
			fmt.Printf("[WARNING]: Failed to apply scaledobjects: %v (You may need to wait a minute for KEDA CRDs to register and retry)\n", err)
		}

		fmt.Println("\n[CLI]: Cluster Setup Complete! Your 'Bring Your Own Render Farm' is ready.")
		fmt.Println("       Make sure to deploy your Coordinator and Workers using your Kubernetes manifests.")
	},
}

// teardownCmd deletes the active GKE cluster to stop billing
var teardownCmd = &cobra.Command{
	Use:   "teardown",
	Short: "Delete the GKE cluster to stop billing",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Printf("[CLI]: Tearing down GKE cluster '%s' in %s...\n", clusterName, region)
		fmt.Println("       This will permanently delete the cluster and stop all associated compute billing. Please wait...")

		c := exec.Command("gcloud", "container", "clusters", "delete", clusterName, "--region", region, "--quiet")
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr

		if err := c.Run(); err != nil {
			fmt.Printf("[ERROR]: Failed to delete cluster: %v\n", err)
			os.Exit(1)
		}

		fmt.Println("[CLI]: Successfully deleted GKE cluster. Billing has been stopped.")
	},
}

func init() {
	rootCmd.AddCommand(cloudCmd)
	cloudCmd.AddCommand(provisionCmd)
	cloudCmd.AddCommand(setupCmd)
	cloudCmd.AddCommand(teardownCmd)

	provisionCmd.Flags().StringVar(&projectID, "project", "", "Your GCP Project ID")
	provisionCmd.MarkFlagRequired("project")
	provisionCmd.Flags().StringVar(&region, "region", "us-central1", "GCP Region for the cluster")
	provisionCmd.Flags().StringVar(&clusterName, "name", "skewer-farm", "Name of the GKE cluster")

	setupCmd.Flags().StringVar(&credsFile, "creds", "", "Path to your GCP Service Account JSON key")
	setupCmd.MarkFlagRequired("creds")

	teardownCmd.Flags().StringVar(&region, "region", "us-central1", "GCP Region for the cluster")
	teardownCmd.Flags().StringVar(&clusterName, "name", "skewer-farm", "Name of the GKE cluster to delete")
}

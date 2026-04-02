package coordinator

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"

	"cloud.google.com/go/storage"
	"google.golang.org/api/option"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	k8serrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/tools/clientcmd"
)

type CloudManager interface {
	EnsureCapacity(ctx context.Context, workerType string, workerCount int) error
	ProvisionStorage(ctx context.Context, bucketName string) error
	DownloadFile(ctx context.Context, bucket, object, destPath string) error
	HasStorageClient() bool
}

type K8sCloudManager struct {
	k8sClient       *kubernetes.Clientset
	storageClient   *storage.Client
	credentialsFile string
}

// NewK8sCloudManager initializes a new CloudManager using Kubernetes and GCP.
// It detects whether it is running inside a cluster (GKE) or outside (Minikube).
// It also takes a path to a user-provided GCP Service Account JSON key file.
func NewK8sCloudManager(ctx context.Context, credentialsFile string) (*K8sCloudManager, error) {
	var config *rest.Config
	var err error

	// Try in-cluster configuration first (e.g. running inside GKE)
	config, err = rest.InClusterConfig()
	if err != nil {
		// Fallback to out-of-cluster config (e.g. Minikube on laptop)
		kubeconfig := os.Getenv("KUBECONFIG")
		if kubeconfig == "" {
			homeDir, err := os.UserHomeDir()
			if err != nil {
				return nil, fmt.Errorf("[ERROR]: Failed to get user home dir: %w", err)
			}
			kubeconfig = filepath.Join(homeDir, ".kube", "config")
		}

		config, err = clientcmd.BuildConfigFromFlags("", kubeconfig)
		if err != nil {
			return nil, fmt.Errorf("[ERROR]: Failed to build kubeconfig: %w", err)
		}
	}

	clientset, err := kubernetes.NewForConfig(config)
	if err != nil {
		return nil, fmt.Errorf("[ERROR]: Failed to create k8s clientset: %w", err)
	}

	var storageClient *storage.Client
	if credentialsFile != "" {
		log.Printf("[CLOUD]: Initializing GCP Storage client with provided credentials: %s", credentialsFile)
		storageClient, err = storage.NewClient(ctx, option.WithCredentialsFile(credentialsFile))
		if err != nil {
			return nil, fmt.Errorf("failed to initialize GCP storage client: %w", err)
		}
	} else {
		// Try Application Default Credentials (works on GKE with Workload Identity
		// or node service account)
		log.Printf("[CLOUD]: No explicit credentials provided. Trying Application Default Credentials...")
		storageClient, err = storage.NewClient(ctx)
		if err != nil {
			log.Printf("[CLOUD]: Warning: Could not initialize GCP storage client with ADC: %v", err)
			log.Printf("[CLOUD]: GCS downloads will not be available.")
		}
	}

	return &K8sCloudManager{
		k8sClient:       clientset,
		storageClient:   storageClient,
		credentialsFile: credentialsFile,
	}, nil
}

// EnsureCapacity creates or updates a Kubernetes Deployment for the C++ workers.
func (c *K8sCloudManager) EnsureCapacity(ctx context.Context, deploymentName string, workerCount int) error {
	log.Printf("[CLOUD]: Ensuring capacity for %s: configuring %d workers...", deploymentName, workerCount)

	deploymentsClient := c.k8sClient.AppsV1().Deployments("default")

	// Look up the existing deployment
	deployment, err := deploymentsClient.Get(ctx, deploymentName, metav1.GetOptions{})

	replicas := int32(workerCount)

	// Define volumes and mounts dynamically based on whether we are local or in the cloud.
	var volumes []corev1.Volume
	var volumeMounts []corev1.VolumeMount
	var envVars []corev1.EnvVar

	envVars = append(envVars, corev1.EnvVar{
		Name:  "COORDINATOR_ADDR",
		Value: "skewer-coordinator.default.svc.cluster.local:50051",
	})
	envVars = append(envVars, corev1.EnvVar{
		Name:  "COORDINATOR_ADDRESS",
		Value: "skewer-coordinator.default.svc.cluster.local:50051",
	})

	if c.credentialsFile == "" {
		// LOCAL MODE: Mount a local hostPath directory to /data
		localDataPath := os.Getenv("LOCAL_DATA_PATH")
		if localDataPath == "" {
			localDataPath = "/data" // fallback
		}
		volumes = append(volumes, corev1.Volume{
			Name: "skewer-data",
			VolumeSource: corev1.VolumeSource{
				HostPath: &corev1.HostPathVolumeSource{
					Path: localDataPath,
				},
			},
		})
		volumeMounts = append(volumeMounts, corev1.VolumeMount{
			Name:      "skewer-data",
			MountPath: "/data",
		})
	} else {
		// CLOUD MODE: Mount the secret to /etc/secrets
		volumes = append(volumes, corev1.Volume{
			Name: "gcp-credentials",
			VolumeSource: corev1.VolumeSource{
				Secret: &corev1.SecretVolumeSource{
					SecretName: "skewer-gcp-creds",
				},
			},
		})
		volumeMounts = append(volumeMounts, corev1.VolumeMount{
			Name:      "gcp-credentials",
			MountPath: "/etc/secrets",
			ReadOnly:  true,
		})
		envVars = append(envVars, corev1.EnvVar{
			Name:  "GOOGLE_APPLICATION_CREDENTIALS",
			Value: "/etc/secrets/credentials.json",
		})
	}

	if k8serrors.IsNotFound(err) {
		// Create a new Deployment if it doesn't exist
		newDeployment := &appsv1.Deployment{
			ObjectMeta: metav1.ObjectMeta{
				Name: deploymentName,
			},
			Spec: appsv1.DeploymentSpec{
				Replicas: &replicas,
				Selector: &metav1.LabelSelector{
					MatchLabels: map[string]string{
						"app": deploymentName,
					},
				},
				Template: corev1.PodTemplateSpec{
					ObjectMeta: metav1.ObjectMeta{
						Labels: map[string]string{
							"app": deploymentName,
						},
					},
					Spec: corev1.PodSpec{
						Containers: []corev1.Container{
							{
								Name:            "worker",
								Image:           deploymentName + ":latest",
								ImagePullPolicy: corev1.PullIfNotPresent,
								Env:             envVars,
								VolumeMounts:    volumeMounts,
							},
						},
						Volumes: volumes,
					},
				},
			},
		}

		_, err = deploymentsClient.Create(ctx, newDeployment, metav1.CreateOptions{})
		if err != nil {
			return fmt.Errorf("[ERROR]: Failed to create deployment %s: %w", deploymentName, err)
		}
		log.Printf("[CLOUD]: Created new deployment %s with %d replicas.", deploymentName, workerCount)
	} else if err != nil {
		return fmt.Errorf("[ERROR]: Failed to get deployment %s: %w", deploymentName, err)
	} else {
		// Update existing Deployment
		if *deployment.Spec.Replicas != replicas {
			deployment.Spec.Replicas = &replicas
			_, err = deploymentsClient.Update(ctx, deployment, metav1.UpdateOptions{})
			if err != nil {
				return fmt.Errorf("[ERROR]: Failed to update deployment %s replicas: %w", deploymentName, err)
			}
			log.Printf("[CLOUD]: Updated deployment %s to %d replicas.", deploymentName, workerCount)
		} else {
			log.Printf("[CLOUD]: Deployment %s already at %d replicas. No change needed.", deploymentName, workerCount)
		}
	}

	return nil
}

// HasStorageClient returns true if GCS operations are available.
func (c *K8sCloudManager) HasStorageClient() bool {
	return c.storageClient != nil
}

// DownloadFile downloads a single object from GCS to a local path.
// It creates parent directories as needed and skips the download if the file already exists.
func (c *K8sCloudManager) DownloadFile(ctx context.Context, bucket, object, destPath string) error {
	if c.storageClient == nil {
		return fmt.Errorf("GCS storage client not initialized")
	}

	// Skip if already downloaded
	if _, err := os.Stat(destPath); err == nil {
		log.Printf("[CLOUD]: File already exists locally, skipping download: %s", destPath)
		return nil
	}

	if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
		return fmt.Errorf("failed to create directory for %s: %w", destPath, err)
	}

	reader, err := c.storageClient.Bucket(bucket).Object(object).NewReader(ctx)
	if err != nil {
		return fmt.Errorf("failed to read gs://%s/%s: %w", bucket, object, err)
	}
	defer reader.Close()

	f, err := os.Create(destPath)
	if err != nil {
		return fmt.Errorf("failed to create local file %s: %w", destPath, err)
	}
	defer f.Close()

	written, err := io.Copy(f, reader)
	if err != nil {
		return fmt.Errorf("failed to download gs://%s/%s: %w", bucket, object, err)
	}

	log.Printf("[CLOUD]: Downloaded gs://%s/%s -> %s (%d bytes)", bucket, object, destPath, written)
	return nil
}

// ProvisionStorage verifies access to the user's GCP bucket and configures storage.
func (c *K8sCloudManager) ProvisionStorage(ctx context.Context, bucketName string) error {
	if c.storageClient == nil {
		return fmt.Errorf("[ERROR]: GCP storage client not initialized; missing credentials")
	}

	log.Printf("[CLOUD]: Verifying storage access to bucket: gs://%s", bucketName)

	// A simple check to ensure we can list objects or get bucket metadata.
	bucket := c.storageClient.Bucket(bucketName)
	attrs, err := bucket.Attrs(ctx)
	if err != nil {
		return fmt.Errorf("[ERROR]: Failed to access user bucket (gs://%s): %w", bucketName, err)
	}

	log.Printf("[CLOUD]: Successfully verified access to target bucket gs://%s (Location: %s)", attrs.Name, attrs.Location)
	return nil
}

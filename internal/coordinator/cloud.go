package coordinator

import (
	"context"
	"fmt"
	"log/slog"
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
		slog.Info("initializing GCP storage client", "credentials_file", credentialsFile)
		// Use the modern WithAuthCredentialsFile option
		storageClient, err = storage.NewClient(ctx, option.WithAuthCredentialsFile(option.ServiceAccount, credentialsFile))
		if err != nil {
			return nil, fmt.Errorf("failed to initialize GCP storage client: %w", err)
		}
	} else {
		slog.Warn("no GCP credentials provided, storage provisioning will fail")
	}

	return &K8sCloudManager{
		k8sClient:       clientset,
		storageClient:   storageClient,
		credentialsFile: credentialsFile,
	}, nil
}

// EnsureCapacity creates or updates a Kubernetes Deployment for the C++ workers.
func (c *K8sCloudManager) EnsureCapacity(ctx context.Context, deploymentName string, workerCount int) error {
	slog.Info("ensuring worker capacity", "deployment", deploymentName, "count", workerCount)

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
		slog.Info("created deployment", "deployment", deploymentName, "replicas", workerCount)
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
			slog.Info("updated deployment replicas", "deployment", deploymentName, "replicas", workerCount)
		} else {
			slog.Debug("deployment replicas unchanged", "deployment", deploymentName, "replicas", workerCount)
		}
	}

	return nil
}

// ProvisionStorage verifies access to the user's GCP bucket and configures storage.
func (c *K8sCloudManager) ProvisionStorage(ctx context.Context, bucketName string) error {
	if c.storageClient == nil {
		return fmt.Errorf("[ERROR]: GCP storage client not initialized; missing credentials")
	}

	slog.Info("verifying GCS bucket access", "bucket", bucketName)

	// A simple check to ensure we can list objects or get bucket metadata.
	bucket := c.storageClient.Bucket(bucketName)
	attrs, err := bucket.Attrs(ctx)
	if err != nil {
		return fmt.Errorf("[ERROR]: Failed to access user bucket (gs://%s): %w", bucketName, err)
	}

	slog.Info("GCS bucket verified", "bucket", attrs.Name, "location", attrs.Location)
	return nil
}

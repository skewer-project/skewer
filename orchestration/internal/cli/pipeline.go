package cli

import (
	"context"
	"crypto/tls"
	"fmt"
	"os/exec"
	"strings"

	"github.com/spf13/cobra"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"golang.org/x/oauth2"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/insecure"
	grpcoauth "google.golang.org/grpc/credentials/oauth"
)

var pipelineCmd = &cobra.Command{
	Use:   "pipeline",
	Short: "Manage rendering pipelines",
}

var (
	pipelineSceneURI string
	pipelineOutput   string
	pipelineCache    bool
	pipelineID       string
	impersonateSA    string
)

var submitCmd = &cobra.Command{
	Use:   "submit",
	Short: "Submit a new rendering pipeline",
	Long: `Submit a rendering pipeline from a root scene.json.

The coordinator downloads and parses the scene, classifies each layer as
static or animated, derives num_frames from the animation block, and
orchestrates Cloud Batch rendering automatically.

Example:
  skewer pipeline submit \
    --scene gs://my-bucket/scenes/shot01/scene.json \
    --output gs://my-bucket/composites/shot01/`,
	RunE: func(cmd *cobra.Command, args []string) error {
		if pipelineSceneURI == "" {
			return fmt.Errorf("--scene is required")
		}
		if !strings.HasPrefix(pipelineSceneURI, "gs://") {
			return fmt.Errorf("--scene must be a gs:// URI")
		}
		if pipelineOutput == "" {
			return fmt.Errorf("--output is required")
		}

		conn, err := dial(coordinatorAddr, useTLS)
		if err != nil {
			return err
		}
		defer conn.Close()

		client := pb.NewCoordinatorServiceClient(conn)
		resp, err := client.SubmitPipeline(context.Background(), &pb.SubmitPipelineRequest{
			SceneUri:                 pipelineSceneURI,
			CompositeOutputUriPrefix: pipelineOutput,
			EnableCache:              pipelineCache,
		})
		if err != nil {
			return fmt.Errorf("SubmitPipeline: %w", err)
		}

		fmt.Printf("Pipeline submitted.\nID: %s\n", resp.PipelineId)
		return nil
	},
}

var statusCmd = &cobra.Command{
	Use:   "status",
	Short: "Get the status of a pipeline",
	RunE: func(cmd *cobra.Command, args []string) error {
		if pipelineID == "" {
			return fmt.Errorf("--pipeline is required")
		}

		conn, err := dial(coordinatorAddr, useTLS)
		if err != nil {
			return err
		}
		defer conn.Close()

		client := pb.NewCoordinatorServiceClient(conn)
		resp, err := client.GetPipelineStatus(context.Background(), &pb.GetPipelineStatusRequest{
			PipelineId: pipelineID,
		})
		if err != nil {
			return fmt.Errorf("GetPipelineStatus: %w", err)
		}

		fmt.Printf("Status: %s\n", resp.Status)
		if resp.ErrorMessage != "" {
			fmt.Printf("Error:  %s\n", resp.ErrorMessage)
		}
		for layerID, prefix := range resp.LayerOutputs {
			fmt.Printf("Layer %s: %s\n", layerID, prefix)
		}
		if resp.CompositeOutput != "" {
			fmt.Printf("Composite: %s\n", resp.CompositeOutput)
		}
		return nil
	},
}

var cancelCmd = &cobra.Command{
	Use:   "cancel",
	Short: "Cancel a running pipeline",
	RunE: func(cmd *cobra.Command, args []string) error {
		if pipelineID == "" {
			return fmt.Errorf("--pipeline is required")
		}

		conn, err := dial(coordinatorAddr, useTLS)
		if err != nil {
			return err
		}
		defer conn.Close()

		client := pb.NewCoordinatorServiceClient(conn)
		resp, err := client.CancelPipeline(context.Background(), &pb.CancelPipelineRequest{
			PipelineId: pipelineID,
		})
		if err != nil {
			return fmt.Errorf("CancelPipeline: %w", err)
		}

		if resp.Success {
			fmt.Println("Pipeline cancelled.")
		} else {
			fmt.Println("Cancel request sent (pipeline may have already finished).")
		}
		return nil
	},
}

// gcloudIdentityTokenSource fetches identity tokens via gcloud, optionally impersonating
// a service account. Impersonation is required for user accounts because Cloud Run needs
// an identity token with aud=service-URL, which user credentials cannot generate directly.
type gcloudIdentityTokenSource struct {
	audience      string
	impersonateSA string
}

func (g gcloudIdentityTokenSource) Token() (*oauth2.Token, error) {
	args := []string{"auth", "print-identity-token", "--audiences=" + g.audience}
	if g.impersonateSA != "" {
		args = append(args, "--impersonate-service-account="+g.impersonateSA)
	}
	out, err := exec.Command("gcloud", args...).Output()
	if err != nil {
		return nil, fmt.Errorf("gcloud auth print-identity-token: %w", err)
	}
	return &oauth2.Token{AccessToken: strings.TrimSpace(string(out))}, nil
}

func dial(addr string, tls_ bool) (*grpc.ClientConn, error) {
	opts := []grpc.DialOption{}
	if tls_ {
		opts = append(opts, grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{})))

		audience := "https://" + strings.Split(addr, ":")[0]
		opts = append(opts, grpc.WithPerRPCCredentials(grpcoauth.TokenSource{
			TokenSource: oauth2.ReuseTokenSource(nil, gcloudIdentityTokenSource{
				audience:      audience,
				impersonateSA: impersonateSA,
			}),
		}))
	} else {
		opts = append(opts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	}
	conn, err := grpc.NewClient(addr, opts...)
	if err != nil {
		return nil, fmt.Errorf("connect to coordinator at %s: %w", addr, err)
	}
	return conn, nil
}

func init() {
	submitCmd.Flags().StringVarP(&pipelineSceneURI, "scene", "s", "", "GCS URI of root scene.json (required, e.g. gs://bucket/scenes/scene.json)")
	submitCmd.Flags().StringVarP(&pipelineOutput, "output", "o", "", "GCS URI prefix for composited output (required)")
	submitCmd.Flags().BoolVar(&pipelineCache, "cache", true, "Enable content-hash layer caching")

	statusCmd.Flags().StringVar(&pipelineID, "pipeline", "", "Pipeline ID to query (required)")
	cancelCmd.Flags().StringVar(&pipelineID, "pipeline", "", "Pipeline ID to cancel (required)")

	pipelineCmd.PersistentFlags().StringVar(&impersonateSA, "impersonate-sa", "",
		"Service account email to impersonate when fetching identity tokens (required for user accounts hitting Cloud Run)")

	pipelineCmd.AddCommand(submitCmd, statusCmd, cancelCmd)
}

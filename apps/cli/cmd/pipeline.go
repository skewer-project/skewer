package cmd

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
	pipelineLayerFlags []string
	pipelineFrames     int32
	pipelineOutput     string
	pipelineCache      bool
	pipelineID         string
	impersonateSA      string

	camFromX, camFromY, camFromZ float32
	camAtX, camAtY, camAtZ       float32
	camVupX, camVupY, camVupZ    float32
	camVfov                      float32
)

var submitCmd = &cobra.Command{
	Use:   "submit",
	Short: "Submit a new rendering pipeline",
	Long: `Submit a multi-layer rendering pipeline.

Each --layer flag specifies one render layer as "layer_id:gs://bucket/scenes/scene.json".

Example:
  skewer pipeline submit \
    --layer smoke:gs://my-bucket/scenes/smoke.json \
    --layer character:gs://my-bucket/scenes/character.json \
    --frames 120 \
    --output gs://my-bucket/composites/shot01/`,
	RunE: func(cmd *cobra.Command, args []string) error {
		if len(pipelineLayerFlags) == 0 {
			return fmt.Errorf("at least one --layer is required")
		}
		if pipelineFrames <= 0 {
			return fmt.Errorf("--frames must be a positive integer")
		}
		if pipelineOutput == "" {
			return fmt.Errorf("--output is required")
		}

		layers := make([]*pb.PipelineLayer, 0, len(pipelineLayerFlags))
		for _, flag := range pipelineLayerFlags {
			idx := strings.Index(flag, ":")
			if idx < 0 {
				return fmt.Errorf("invalid --layer %q: expected format \"layer_id:gs://bucket/scene.json\"", flag)
			}
			layers = append(layers, &pb.PipelineLayer{
				LayerId:     flag[:idx],
				SceneUri:    flag[idx+1:],
				EnableCache: pipelineCache,
			})
		}

		conn, err := dial(coordinatorAddr, useTLS)
		if err != nil {
			return err
		}
		defer conn.Close()

		client := pb.NewCoordinatorServiceClient(conn)
		resp, err := client.SubmitPipeline(context.Background(), &pb.SubmitPipelineRequest{
			NumFrames:                pipelineFrames,
			Layers:                   layers,
			CompositeOutputUriPrefix: pipelineOutput,
			Camera: &pb.CameraParams{
				LookFrom: []float32{camFromX, camFromY, camFromZ},
				LookAt:   []float32{camAtX, camAtY, camAtZ},
				Vup:      []float32{camVupX, camVupY, camVupZ},
				Vfov:     camVfov,
			},
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
	submitCmd.Flags().StringArrayVarP(&pipelineLayerFlags, "layer", "l", nil, "Layer to render as \"layer_id:gs://bucket/scene.json\" (repeatable)")
	submitCmd.Flags().Int32VarP(&pipelineFrames, "frames", "f", 1, "Number of frames to render")
	submitCmd.Flags().StringVarP(&pipelineOutput, "output", "o", "", "GCS URI prefix for composited output (required)")
	submitCmd.Flags().BoolVar(&pipelineCache, "cache", true, "Enable content-hash layer caching")

	submitCmd.Flags().Float32Var(&camFromX, "cam-from-x", 0, "Camera look_from X")
	submitCmd.Flags().Float32Var(&camFromY, "cam-from-y", 0, "Camera look_from Y")
	submitCmd.Flags().Float32Var(&camFromZ, "cam-from-z", 5, "Camera look_from Z")
	submitCmd.Flags().Float32Var(&camAtX, "cam-at-x", 0, "Camera look_at X")
	submitCmd.Flags().Float32Var(&camAtY, "cam-at-y", 0, "Camera look_at Y")
	submitCmd.Flags().Float32Var(&camAtZ, "cam-at-z", 0, "Camera look_at Z")
	submitCmd.Flags().Float32Var(&camVupX, "cam-vup-x", 0, "Camera up vector X")
	submitCmd.Flags().Float32Var(&camVupY, "cam-vup-y", 1, "Camera up vector Y")
	submitCmd.Flags().Float32Var(&camVupZ, "cam-vup-z", 0, "Camera up vector Z")
	submitCmd.Flags().Float32Var(&camVfov, "cam-vfov", 90, "Camera vertical FOV in degrees")

	statusCmd.Flags().StringVar(&pipelineID, "pipeline", "", "Pipeline ID to query (required)")
	cancelCmd.Flags().StringVar(&pipelineID, "pipeline", "", "Pipeline ID to cancel (required)")

	pipelineCmd.PersistentFlags().StringVar(&impersonateSA, "impersonate-sa", "",
		"Service account email to impersonate when fetching identity tokens (required for user accounts hitting Cloud Run)")

	pipelineCmd.AddCommand(submitCmd, statusCmd, cancelCmd)
}

package api

import (
	"context"
	"crypto/tls"
	"fmt"
	"net/url"
	"strings"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"google.golang.org/api/idtoken"
	"google.golang.org/api/option"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	grpcoauth "google.golang.org/grpc/credentials/oauth"
)

// CoordinatorClient is a thin wrapper around the generated gRPC client that
// dials the skewer-coordinator Cloud Run service with Google ID-token auth.
// On Cloud Run the API service account uses its metadata-server credentials;
// grant the API SA roles/run.invoker on the coordinator service.
type CoordinatorClient struct {
	conn *grpc.ClientConn
	rpc  pb.CoordinatorServiceClient
}

// NewCoordinatorClient creates a client configured for the given Cloud Run
// coordinator URL (e.g. https://skewer-coordinator-xxx.run.app). The audience
// used for the ID token is derived from the URL.
func NewCoordinatorClient(ctx context.Context, coordinatorURL string) (*CoordinatorClient, error) {
	if coordinatorURL == "" {
		return nil, fmt.Errorf("coordinatorURL is required")
	}
	parsed, err := url.Parse(coordinatorURL)
	if err != nil {
		return nil, fmt.Errorf("parse coordinator URL %q: %w", coordinatorURL, err)
	}
	if parsed.Scheme != "https" {
		return nil, fmt.Errorf("coordinator URL must use https: %q", coordinatorURL)
	}
	host := parsed.Host
	if host == "" {
		return nil, fmt.Errorf("coordinator URL has no host: %q", coordinatorURL)
	}

	// audience for the ID token is the origin (scheme + host), no path.
	audience := parsed.Scheme + "://" + host

	// ID tokens are obtained through the google-api library; works on Cloud
	// Run using metadata-server credentials out of the box.
	ts, err := idtoken.NewTokenSource(ctx, audience, option.WithAudiences(audience))
	if err != nil {
		return nil, fmt.Errorf("create ID token source (audience=%s): %w", audience, err)
	}

	// Cloud Run HTTP/2 gRPC endpoint listens on :443 with TLS.
	addr := host
	if !strings.Contains(addr, ":") {
		addr = addr + ":443"
	}

	conn, err := grpc.NewClient(
		addr,
		grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{ServerName: parsed.Host})),
		grpc.WithPerRPCCredentials(grpcoauth.TokenSource{TokenSource: ts}),
	)
	if err != nil {
		return nil, fmt.Errorf("dial coordinator at %s: %w", addr, err)
	}

	return &CoordinatorClient{conn: conn, rpc: pb.NewCoordinatorServiceClient(conn)}, nil
}

func (c *CoordinatorClient) Submit(ctx context.Context, req *pb.SubmitPipelineRequest) (*pb.SubmitPipelineResponse, error) {
	return c.rpc.SubmitPipeline(ctx, req)
}

func (c *CoordinatorClient) Status(ctx context.Context, pipelineID string) (*pb.GetPipelineStatusResponse, error) {
	return c.rpc.GetPipelineStatus(ctx, &pb.GetPipelineStatusRequest{PipelineId: pipelineID})
}

func (c *CoordinatorClient) Cancel(ctx context.Context, pipelineID string) (*pb.CancelPipelineResponse, error) {
	return c.rpc.CancelPipeline(ctx, &pb.CancelPipelineRequest{PipelineId: pipelineID})
}

func (c *CoordinatorClient) Close() error {
	return c.conn.Close()
}

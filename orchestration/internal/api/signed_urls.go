package api

import (
	"context"
	"fmt"
	"time"

	"cloud.google.com/go/storage"

	credentials "cloud.google.com/go/iam/credentials/apiv1"
	credentialspb "cloud.google.com/go/iam/credentials/apiv1/credentialspb"
)

// URLSigner produces V4-signed GCS URLs via the IAM SignBlob API, without
// needing a private key file on disk. On Cloud Run the API service account
// is granted roles/iam.serviceAccountTokenCreator on itself so that SignBlob
// calls succeed using metadata-server credentials.
type URLSigner struct {
	iam     *credentials.IamCredentialsClient
	saEmail string
}

func NewURLSigner(ctx context.Context, saEmail string) (*URLSigner, error) {
	if saEmail == "" {
		return nil, fmt.Errorf("saEmail is required")
	}
	iam, err := credentials.NewIamCredentialsClient(ctx)
	if err != nil {
		return nil, fmt.Errorf("create iam credentials client: %w", err)
	}
	return &URLSigner{iam: iam, saEmail: saEmail}, nil
}

// SignPut returns a V4-signed PUT URL valid for the given duration.
// contentType may be empty.
func (s *URLSigner) SignPut(bucket, object, contentType string, ttl time.Duration) (string, error) {
	opts := &storage.SignedURLOptions{
		Scheme:         storage.SigningSchemeV4,
		Method:         "PUT",
		Expires:        time.Now().Add(ttl),
		GoogleAccessID: s.saEmail,
		SignBytes:      s.signBytes,
	}
	if contentType != "" {
		opts.ContentType = contentType
	}
	u, err := storage.SignedURL(bucket, object, opts)
	if err != nil {
		return "", fmt.Errorf("sign PUT url for %s/%s: %w", bucket, object, err)
	}
	return u, nil
}

// SignGet returns a V4-signed GET URL valid for the given duration.
func (s *URLSigner) SignGet(bucket, object string, ttl time.Duration) (string, error) {
	opts := &storage.SignedURLOptions{
		Scheme:         storage.SigningSchemeV4,
		Method:         "GET",
		Expires:        time.Now().Add(ttl),
		GoogleAccessID: s.saEmail,
		SignBytes:      s.signBytes,
	}
	u, err := storage.SignedURL(bucket, object, opts)
	if err != nil {
		return "", fmt.Errorf("sign GET url for %s/%s: %w", bucket, object, err)
	}
	return u, nil
}

// SanitizeObjectPath rejects absolute paths, path-traversal segments, and
// empty components. Returns the cleaned path safe to use as a GCS object key.
func SanitizeObjectPath(p string) (string, error) {
	if p == "" {
		return "", fmt.Errorf("empty path")
	}
	if p[0] == '/' {
		return "", fmt.Errorf("path must be relative: %q", p)
	}
	// Reject dot-segments and empty segments.
	for start := 0; start < len(p); {
		end := start
		for end < len(p) && p[end] != '/' {
			end++
		}
		seg := p[start:end]
		if seg == "" || seg == "." || seg == ".." {
			return "", fmt.Errorf("invalid path segment %q in %q", seg, p)
		}
		start = end + 1
	}
	return p, nil
}

func (s *URLSigner) signBytes(b []byte) ([]byte, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	resp, err := s.iam.SignBlob(ctx, &credentialspb.SignBlobRequest{
		Name:    "projects/-/serviceAccounts/" + s.saEmail,
		Payload: b,
	})
	if err != nil {
		return nil, fmt.Errorf("iam.SignBlob: %w", err)
	}
	return resp.SignedBlob, nil
}

func (s *URLSigner) Close() error {
	return s.iam.Close()
}

package api

import (
	"context"
	"fmt"
	"net/http"
	"strings"

	firebase "firebase.google.com/go/v4"
	firebaseauth "firebase.google.com/go/v4/auth"
	"google.golang.org/api/option"
)

// AuthVerifier validates Firebase ID tokens and enforces the admin-email
// allowlist.
type AuthVerifier struct {
	client   *firebaseauth.Client
	allowed  map[string]struct{}
	anyEmail bool
}

// NewAuthVerifier creates a Firebase Auth client. `projectID` is the Firebase
// project ID (typically equal to the GCP project ID). `adminEmails` is a comma-
// separated list of case-insensitive emails permitted to call the API. If the
// list is empty, ALL authenticated emails are rejected (fail-closed).
func NewAuthVerifier(ctx context.Context, projectID, adminEmails string) (*AuthVerifier, error) {
	if projectID == "" {
		return nil, fmt.Errorf("firebase project ID is required")
	}

	app, err := firebase.NewApp(ctx, &firebase.Config{ProjectID: projectID}, option.WithoutAuthentication())
	if err != nil {
		return nil, fmt.Errorf("init firebase app: %w", err)
	}

	client, err := app.Auth(ctx)
	if err != nil {
		return nil, fmt.Errorf("init firebase auth client: %w", err)
	}

	allowed := map[string]struct{}{}
	for _, e := range strings.Split(adminEmails, ",") {
		e = strings.TrimSpace(strings.ToLower(e))
		if e != "" {
			allowed[e] = struct{}{}
		}
	}

	return &AuthVerifier{client: client, allowed: allowed}, nil
}

type ctxKey int

const (
	ctxKeyEmail ctxKey = iota
)

// Middleware verifies the Bearer token on every request except those whose
// path matches `skipPaths` (e.g. /healthz).
func (a *AuthVerifier) Middleware(skipPaths ...string) func(http.Handler) http.Handler {
	skip := make(map[string]struct{}, len(skipPaths))
	for _, p := range skipPaths {
		skip[p] = struct{}{}
	}
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if _, ok := skip[r.URL.Path]; ok {
				next.ServeHTTP(w, r)
				return
			}
			if r.Method == http.MethodOptions {
				next.ServeHTTP(w, r)
				return
			}

			authHeader := r.Header.Get("Authorization")
			tok := strings.TrimPrefix(authHeader, "Bearer ")
			if tok == "" || tok == authHeader {
				httpError(w, http.StatusUnauthorized, "missing bearer token")
				return
			}

			verified, err := a.client.VerifyIDToken(r.Context(), tok)
			if err != nil {
				httpError(w, http.StatusUnauthorized, fmt.Sprintf("invalid token: %v", err))
				return
			}

			emailClaim, _ := verified.Claims["email"].(string)
			emailVerified, _ := verified.Claims["email_verified"].(bool)
			email := strings.ToLower(strings.TrimSpace(emailClaim))
			if email == "" || !emailVerified {
				httpError(w, http.StatusForbidden, "token has no verified email")
				return
			}

			if _, ok := a.allowed[email]; !ok {
				httpError(w, http.StatusForbidden, "email not authorized")
				return
			}

			ctx := context.WithValue(r.Context(), ctxKeyEmail, email)
			next.ServeHTTP(w, r.WithContext(ctx))
		})
	}
}

// EmailFromContext returns the authenticated email stored by Middleware.
func EmailFromContext(ctx context.Context) string {
	v, _ := ctx.Value(ctxKeyEmail).(string)
	return v
}

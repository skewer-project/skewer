package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"

	"cloud.google.com/go/storage"

	skapi "github.com/skewer-project/skewer/orchestration/internal/api"
)

func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	port := getEnvOrDefault("PORT", "8080")

	dataBucket := mustEnv("DATA_BUCKET")
	coordinatorURL := mustEnv("COORDINATOR_URL")
	apiSAEmail := mustEnv("API_SA_EMAIL")
	firebaseProject := mustEnv("FIREBASE_PROJECT")
	adminEmails := os.Getenv("ADMIN_EMAILS")
	corsOrigins := os.Getenv("CORS_ALLOWED_ORIGINS")
	initPerHour := getEnvIntOrDefault("RATE_INIT_PER_HOUR", 60)
	submitPerHour := getEnvIntOrDefault("RATE_SUBMIT_PER_HOUR", 5)

	storageClient, err := storage.NewClient(ctx)
	if err != nil {
		log.Fatalf("[API]: init storage client: %v", err)
	}
	defer storageClient.Close()

	signer, err := skapi.NewURLSigner(ctx, apiSAEmail)
	if err != nil {
		log.Fatalf("[API]: init URL signer: %v", err)
	}
	defer signer.Close()

	owner := skapi.NewOwnerStore(storageClient, dataBucket)
	normalizer := skapi.NewNormalizer(storageClient, dataBucket)

	coordinator, err := skapi.NewCoordinatorClient(ctx, coordinatorURL)
	if err != nil {
		log.Fatalf("[API]: init coordinator client: %v", err)
	}
	defer coordinator.Close()

	auth, err := skapi.NewAuthVerifier(ctx, firebaseProject, adminEmails)
	if err != nil {
		log.Fatalf("[API]: init auth verifier: %v", err)
	}

	limiter := skapi.NewEmailLimiter(initPerHour, submitPerHour)
	go sweepLoop(ctx, limiter)

	server := skapi.NewServer(skapi.Config{
		DataBucket: dataBucket,
	}, storageClient, signer, owner, normalizer, coordinator, limiter)

	mux := http.NewServeMux()
	server.RegisterRoutes(mux)

	// Middleware chain: CORS (outermost) → auth (skip /healthz) → mux.
	var handler http.Handler = mux
	handler = auth.Middleware("/healthz")(handler)
	handler = skapi.CORSMiddleware(corsOrigins)(handler)

	srv := &http.Server{
		Addr:              ":" + port,
		Handler:           handler,
		ReadHeaderTimeout: 10 * time.Second,
		IdleTimeout:       120 * time.Second,
	}

	go func() {
		log.Printf("[API]: listening on :%s", port)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("[API]: server failed: %v", err)
		}
	}()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	<-sigCh

	log.Println("[API]: shutting down...")
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 25*time.Second)
	defer shutdownCancel()
	if err := srv.Shutdown(shutdownCtx); err != nil {
		log.Printf("[API]: graceful shutdown failed: %v", err)
	}
}

func sweepLoop(ctx context.Context, limiter *skapi.EmailLimiter) {
	t := time.NewTicker(30 * time.Minute)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			limiter.Sweep()
		}
	}
}

func mustEnv(key string) string {
	v := os.Getenv(key)
	if v == "" {
		log.Fatalf("[API]: required env var %s is not set", key)
	}
	return v
}

func getEnvOrDefault(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func getEnvIntOrDefault(key string, def int) int {
	v := os.Getenv(key)
	if v == "" {
		return def
	}
	n, err := strconv.Atoi(v)
	if err != nil {
		log.Fatalf("[API]: %s must be an integer: %v", key, err)
	}
	return n
}

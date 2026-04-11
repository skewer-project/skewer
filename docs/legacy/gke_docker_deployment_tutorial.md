# Tutorial: Dockerizing C++ Workers for GKE and GCS Integration

This guide explains how to containerize high-performance C++ applications for Google Kubernetes Engine (GKE), integrate with Google Cloud Storage (GCS), and manage deployments.

---

## 1. Multi-Stage Docker Builds for C++

C++ applications often have heavy build-time dependencies (compilers, headers, build tools) that are unnecessary at runtime. Multi-stage builds allow you to create a tiny, secure runtime image.

### Example: Multi-Stage Dockerfile
```dockerfile
# --- Stage 1: Build ---
FROM debian:bookworm AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libgrpc++-dev protobuf-compiler-grpc \
    libopenexr-dev \
    libgoogle-cloud-storage-dev

WORKDIR /app
COPY . .

# Build the application
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) worker_app

# --- Stage 2: Runtime ---
FROM debian:bookworm-slim

# Install only the runtime libraries needed
RUN apt-get update && apt-get install -y \
    libgrpc++1.51 \
    libopenexr-3-1-30 \
    ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/worker_app .

# Run the worker
ENTRYPOINT ["./worker_app"]
```

---

## 2. Integrating Google Cloud Storage (GCS)

To upload results to `gs://` URIs, your C++ application needs to interface with the Google Cloud API.

### C++ Client Library Example
```cpp
#include "google/cloud/storage/client.h"
namespace gcs = ::google::cloud::storage;

void UploadToGCS(std::string const& bucket_name, std::string const& object_name, std::string const& data) {
    auto client = gcs::Client::CreateDefaultClient().value();
    auto writer = client.WriteObject(bucket_name, object_name);
    writer << data;
    writer.Close();
}
```

### Authentication (Workload Identity)
**Never hardcode API keys or download JSON service account keys into your Docker image.**
In GKE, use **Workload Identity**. This allows a Kubernetes Service Account to "impersonate" a GCP IAM Service Account. The GCS C++ library will automatically find these credentials if configured correctly in the cluster.

---

## 3. Kubernetes Manifests for GKE

Kubernetes uses YAML manifests to define how your containers run.

### Example: deployment.yaml
```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: skewer-worker
spec:
  replicas: 3
  selector:
    matchLabels:
      app: worker
  template:
    metadata:
      labels:
        app: worker
    spec:
      serviceAccountName: skewer-ksa # Linked to GCP IAM via Workload Identity
      containers:
      - name: worker
        image: us-central1-docker.pkg.dev/my-project/my-repo/worker:latest
        ports:
        - containerPort: 50051
        resources:
          limits:
            cpu: "2"
            memory: "4Gi"
          requests:
            cpu: "1"
            memory: "2Gi"
```

### Example: service.yaml
```yaml
apiVersion: v1
kind: Service
metadata:
  name: worker-service
spec:
  selector:
    app: worker
  ports:
    - protocol: TCP
      port: 80
      targetPort: 50051
  type: ClusterIP
```

---

## 4. Architecture Pattern: The Side-Channel Pattern

Since your project involves "Deep Compositing" (large data layers), consider this flow:
1.  **Coordinator (Go)** sends a gRPC request to a **Worker (C++)**.
2.  **Worker** renders a specific layer.
3.  **Worker** uploads the heavy `.exr` or `.kebab` file directly to GCS.
4.  **Worker** returns a gRPC response containing only the **GCS URI** of the result.
5.  **Compositor** later downloads these URIs to merge them.

This prevents massive binary data from clogging the gRPC streams.

---

## 5. Deployment Workflow (Summary)
1.  **Build:** Run Docker build locally or in Cloud Build.
2.  **Push:** Push the image to `gcr.io` or `pkg.dev` (Artifact Registry).
3.  **Apply:** Use `kubectl apply -f manifests/` to deploy to GKE.
4.  **Observe:** Use `kubectl logs` and Google Cloud Console to monitor the workers.

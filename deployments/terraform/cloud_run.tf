locals {
  ar_base = "${var.region}-docker.pkg.dev/${var.project_id}/${google_artifact_registry_repository.skewer.repository_id}"
}

resource "google_cloud_run_v2_service" "coordinator" {
  name     = "skewer-coordinator"
  location = var.region

  template {
    service_account       = google_service_account.coordinator.email
    execution_environment = "EXECUTION_ENVIRONMENT_GEN2"

    containers {
      # Use a public placeholder on first apply so Terraform doesn't block on a
      # missing image. Cloud Build pushes the real image and updates the revision.
      image = "us-docker.pkg.dev/cloudrun/container/hello:latest"

      ports {
        name           = "h2c"
        container_port = 50051
      }

      env {
        name  = "GCP_PROJECT"
        value = var.project_id
      }
      env {
        name  = "GCP_REGION"
        value = var.region
      }
      env {
        name  = "DATA_BUCKET"
        value = google_storage_bucket.data.name
      }
      env {
        name  = "CACHE_BUCKET"
        value = google_storage_bucket.cache.name
      }
      env {
        name  = "WORKFLOW_NAME"
        value = google_workflows_workflow.render_pipeline.id
      }
      env {
        name  = "SKEWER_IMAGE"
        value = "${local.ar_base}/skewer-worker:latest"
      }
      env {
        name  = "LOOM_IMAGE"
        value = "${local.ar_base}/loom-worker:latest"
      }
      env {
        name  = "SKEWER_BATCH_MACHINE_TYPE"
        value = var.skewer_batch_machine_type
      }
      env {
        name  = "SKEWER_BATCH_CPU_MILLI"
        value = tostring(var.skewer_batch_cpu_milli)
      }
      env {
        name  = "SKEWER_BATCH_MEMORY_MIB"
        value = tostring(var.skewer_batch_memory_mib)
      }
      env {
        name  = "SKEWER_BATCH_PROVISIONING_MODEL"
        value = var.skewer_batch_provisioning_model
      }
      env {
        name  = "SKEWER_BATCH_MAX_RETRY_COUNT"
        value = tostring(var.skewer_batch_max_retry_count)
      }
      env {
        name  = "LOOM_BATCH_MACHINE_TYPE"
        value = var.loom_batch_machine_type
      }
      env {
        name  = "LOOM_BATCH_CPU_MILLI"
        value = tostring(var.loom_batch_cpu_milli)
      }
      env {
        name  = "LOOM_BATCH_MEMORY_MIB"
        value = tostring(var.loom_batch_memory_mib)
      }
      env {
        name  = "LOOM_BATCH_PROVISIONING_MODEL"
        value = var.loom_batch_provisioning_model
      }
      env {
        name  = "LOOM_BATCH_MAX_RETRY_COUNT"
        value = tostring(var.loom_batch_max_retry_count)
      }
      env {
        name  = "VPC_NETWORK"
        value = google_compute_network.skewer.self_link
      }
      env {
        name  = "VPC_SUBNET"
        value = google_compute_subnetwork.skewer.self_link
      }
      env {
        name  = "BATCH_SA_EMAIL"
        value = google_service_account.batch_worker.email
      }
    }
  }

  depends_on = [google_project_service.apis]

  lifecycle {
    # Cloud Build manages the deployed image tag. Ignore changes so that
    # `terraform apply` doesn't revert the coordinator back to the placeholder.
    ignore_changes = [template[0].containers[0].image]
  }
}

# ── skewer-api (previewer-facing HTTP service) ────────────────────────────

resource "google_cloud_run_v2_service" "api" {
  name     = "skewer-api"
  location = var.region

  # The browser must be able to reach this service without doing IAM-token
  # gymnastics; auth happens in-handler (Firebase ID token + email allowlist).
  ingress = "INGRESS_TRAFFIC_ALL"

  template {
    service_account                  = google_service_account.api.email
    execution_environment            = "EXECUTION_ENVIRONMENT_GEN2"
    max_instance_request_concurrency = var.api_concurrency

    containers {
      # Placeholder until Cloud Build pushes the real image.
      image = "us-docker.pkg.dev/cloudrun/container/hello:latest"

      ports {
        container_port = 8080
      }

      resources {
        limits = {
          cpu    = "1"
          memory = "512Mi"
        }
      }

      env {
        name  = "GCP_PROJECT"
        value = var.project_id
      }
      env {
        name  = "DATA_BUCKET"
        value = google_storage_bucket.data.name
      }
      env {
        name  = "COORDINATOR_URL"
        value = google_cloud_run_v2_service.coordinator.uri
      }
      env {
        name  = "API_SA_EMAIL"
        value = google_service_account.api.email
      }
      env {
        name  = "FIREBASE_PROJECT"
        value = var.project_id
      }
      env {
        name  = "ADMIN_EMAILS"
        value = join(",", var.admin_emails)
      }
      env {
        name  = "CORS_ALLOWED_ORIGINS"
        value = join(",", var.previewer_cors_origins)
      }
      env {
        name  = "RATE_INIT_PER_HOUR"
        value = tostring(var.api_rate_init_per_hour)
      }
      env {
        name  = "RATE_SUBMIT_PER_HOUR"
        value = tostring(var.api_rate_submit_per_hour)
      }
    }

    scaling {
      max_instance_count = var.api_max_instances
    }
  }

  depends_on = [google_project_service.apis]

  lifecycle {
    ignore_changes = [template[0].containers[0].image]
  }
}

# Public-unauthenticated: the browser must reach this service directly.
# Security is enforced in-handler via Firebase ID token + email allowlist.
resource "google_cloud_run_v2_service_iam_member" "api_public_invoker" {
  project  = var.project_id
  location = var.region
  name     = google_cloud_run_v2_service.api.name
  role     = "roles/run.invoker"
  member   = "allUsers"
}

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

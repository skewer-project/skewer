output "coordinator_url" {
  description = "Cloud Run URL for the gRPC coordinator"
  value       = google_cloud_run_v2_service.coordinator.uri
}

output "data_bucket" {
  description = "GCS bucket for render outputs"
  value       = google_storage_bucket.data.name
}

output "cache_bucket" {
  description = "GCS bucket for content-hash layer cache"
  value       = google_storage_bucket.cache.name
}

output "artifact_registry_path" {
  description = "Artifact Registry base path for Docker images"
  value       = local.ar_base
}

output "workflow_name" {
  description = "Cloud Workflows resource name"
  value       = google_workflows_workflow.render_pipeline.id
}

output "coordinator_sa_email" {
  description = "Service account email for the Cloud Run coordinator"
  value       = google_service_account.coordinator.email
}

output "batch_worker_sa_email" {
  description = "Service account email for Cloud Batch worker VMs"
  value       = google_service_account.batch_worker.email
}

output "api_url" {
  description = "Cloud Run URL for the previewer-facing skewer-api service"
  value       = google_cloud_run_v2_service.api.uri
}

output "api_sa_email" {
  description = "Service account email for skewer-api"
  value       = google_service_account.api.email
}

output "firebase_project_id" {
  description = "Firebase project ID (equal to the GCP project). Supplied to the previewer's Firebase Web SDK config."
  value       = var.project_id
}

output "firebase_auth_domain" {
  description = "Auth domain for the Firebase Web SDK. Standard Identity Platform pattern."
  value       = "${var.project_id}.firebaseapp.com"
}

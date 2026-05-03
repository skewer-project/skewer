# ── Service Accounts ──────────────────────────────────────────────────────────

resource "google_service_account" "coordinator" {
  account_id   = "skewer-coordinator"
  display_name = "Skewer Coordinator (Cloud Run)"
}

resource "google_service_account" "workflow" {
  account_id   = "skewer-workflow"
  display_name = "Skewer Workflow (Cloud Workflows)"
}

resource "google_service_account" "batch_worker" {
  account_id   = "skewer-batch-worker"
  display_name = "Skewer Batch Worker (Cloud Batch VMs)"
}

resource "google_service_account" "api" {
  account_id   = "skewer-api"
  display_name = "Skewer API (Cloud Run, previewer-facing)"
}

# ── Coordinator SA roles ───────────────────────────────────────────────────────

# Allow the coordinator SA to invoke itself (needed for developer impersonation flows)
resource "google_cloud_run_v2_service_iam_member" "coordinator_self_invoker" {
  project  = var.project_id
  location = var.region
  name     = google_cloud_run_v2_service.coordinator.name
  role     = "roles/run.invoker"
  member   = "serviceAccount:${google_service_account.coordinator.email}"
}

# Invoke Cloud Workflows executions
resource "google_project_iam_member" "coordinator_workflows_invoker" {
  project = var.project_id
  role    = "roles/workflows.invoker"
  member  = "serviceAccount:${google_service_account.coordinator.email}"
}

# Read/write renders in the data bucket
resource "google_storage_bucket_iam_member" "coordinator_data_object_user" {
  bucket = google_storage_bucket.data.name
  role   = "roles/storage.objectUser"
  member = "serviceAccount:${google_service_account.coordinator.email}"
}

# Read layer cache keys from the cache bucket
resource "google_storage_bucket_iam_member" "coordinator_cache_object_viewer" {
  bucket = google_storage_bucket.cache.name
  role   = "roles/storage.objectViewer"
  member = "serviceAccount:${google_service_account.coordinator.email}"
}

# ── Workflow SA roles ──────────────────────────────────────────────────────────

# Create and manage Cloud Batch jobs
resource "google_project_iam_member" "workflow_batch_jobs_editor" {
  project = var.project_id
  role    = "roles/batch.jobsEditor"
  member  = "serviceAccount:${google_service_account.workflow.email}"
}

# Read/write both buckets (check cache manifests, write render outputs)
resource "google_storage_bucket_iam_member" "workflow_data_object_user" {
  bucket = google_storage_bucket.data.name
  role   = "roles/storage.objectUser"
  member = "serviceAccount:${google_service_account.workflow.email}"
}

resource "google_storage_bucket_iam_member" "workflow_cache_object_user" {
  bucket = google_storage_bucket.cache.name
  role   = "roles/storage.objectUser"
  member = "serviceAccount:${google_service_account.workflow.email}"
}

# Allow the workflow SA to act as the batch worker SA when submitting jobs
resource "google_service_account_iam_member" "workflow_impersonate_batch" {
  service_account_id = google_service_account.batch_worker.name
  role               = "roles/iam.serviceAccountUser"
  member             = "serviceAccount:${google_service_account.workflow.email}"
}

# ── Batch Worker SA roles ──────────────────────────────────────────────────────

# Allow the Batch agent on worker VMs to report task state to the Batch API
resource "google_project_iam_member" "batch_agent_reporter" {
  project = var.project_id
  role    = "roles/batch.agentReporter"
  member  = "serviceAccount:${google_service_account.batch_worker.email}"
}

# Read/write renders and cache (GCS FUSE mounts)
resource "google_storage_bucket_iam_member" "batch_data_object_user" {
  bucket = google_storage_bucket.data.name
  role   = "roles/storage.objectUser"
  member = "serviceAccount:${google_service_account.batch_worker.email}"
}

resource "google_storage_bucket_iam_member" "batch_cache_object_user" {
  bucket = google_storage_bucket.cache.name
  role   = "roles/storage.objectUser"
  member = "serviceAccount:${google_service_account.batch_worker.email}"
}

# Write structured logs from worker VMs
resource "google_project_iam_member" "batch_log_writer" {
  project = var.project_id
  role    = "roles/logging.logWriter"
  member  = "serviceAccount:${google_service_account.batch_worker.email}"
}

# Pull Docker images from Artifact Registry
resource "google_artifact_registry_repository_iam_member" "batch_ar_reader" {
  repository = google_artifact_registry_repository.skewer.name
  location   = var.region
  role       = "roles/artifactregistry.reader"
  member     = "serviceAccount:${google_service_account.batch_worker.email}"
}

# ── API SA roles ──────────────────────────────────────────────────────────

# Self-impersonation: required so the API can IAM-sign V4 URLs without a
# private-key JSON file on disk.
resource "google_service_account_iam_member" "api_self_token_creator" {
  service_account_id = google_service_account.api.name
  role               = "roles/iam.serviceAccountTokenCreator"
  member             = "serviceAccount:${google_service_account.api.email}"
}

# Invoke the coordinator Cloud Run service over gRPC with an ID token.
resource "google_cloud_run_v2_service_iam_member" "api_coordinator_invoker" {
  project  = var.project_id
  location = var.region
  name     = google_cloud_run_v2_service.coordinator.name
  role     = "roles/run.invoker"
  member   = "serviceAccount:${google_service_account.api.email}"
}

# Read uploaded files, write normalized scene.json + ownership markers,
# write composite output artifact references.
resource "google_storage_bucket_iam_member" "api_data_object_user" {
  bucket = google_storage_bucket.data.name
  role   = "roles/storage.objectUser"
  member = "serviceAccount:${google_service_account.api.email}"
}

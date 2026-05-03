variable "cloudbuild_repository_id" {
  description = "Cloud Build v2 repository resource ID to watch for image rebuilds. Leave null to skip trigger creation."
  type        = string
  default     = null
  nullable    = true
}

variable "cloudbuild_trigger_branch" {
  description = "RE2 branch regex for the image rebuild trigger."
  type        = string
  default     = "^main$"
}

resource "google_service_account" "cloudbuild" {
  account_id   = "skewer-cloud-build"
  display_name = "Skewer Cloud Build"

  depends_on = [google_project_service.apis]
}

resource "google_project_iam_member" "cloudbuild_logs_writer" {
  project = var.project_id
  role    = "roles/logging.logWriter"
  member  = "serviceAccount:${google_service_account.cloudbuild.email}"
}

resource "google_artifact_registry_repository_iam_member" "cloudbuild_ar_writer" {
  repository = google_artifact_registry_repository.skewer.name
  location   = var.region
  role       = "roles/artifactregistry.writer"
  member     = "serviceAccount:${google_service_account.cloudbuild.email}"
}

# Allow Cloud Build to deploy new revisions to the coordinator Cloud Run service
resource "google_cloud_run_v2_service_iam_member" "cloudbuild_run_developer" {
  project  = var.project_id
  location = var.region
  name     = google_cloud_run_v2_service.coordinator.name
  role     = "roles/run.developer"
  member   = "serviceAccount:${google_service_account.cloudbuild.email}"
}

# Cloud Build SA must be able to act as the coordinator SA when deploying
# (gcloud run deploy --service-account requires iam.serviceAccounts.actAs)
resource "google_service_account_iam_member" "cloudbuild_actAs_coordinator" {
  service_account_id = google_service_account.coordinator.name
  role               = "roles/iam.serviceAccountUser"
  member             = "serviceAccount:${google_service_account.cloudbuild.email}"
}

# Allow Cloud Build to deploy new revisions to the skewer-api Cloud Run service
resource "google_cloud_run_v2_service_iam_member" "cloudbuild_api_developer" {
  project  = var.project_id
  location = var.region
  name     = google_cloud_run_v2_service.api.name
  role     = "roles/run.developer"
  member   = "serviceAccount:${google_service_account.cloudbuild.email}"
}

resource "google_service_account_iam_member" "cloudbuild_actAs_api" {
  service_account_id = google_service_account.api.name
  role               = "roles/iam.serviceAccountUser"
  member             = "serviceAccount:${google_service_account.cloudbuild.email}"
}

resource "google_cloudbuild_trigger" "main_image_rebuild" {
  count       = var.cloudbuild_repository_id == null ? 0 : 1
  location    = var.region
  name        = "${local.name_prefix}-main-images"
  description = "Rebuilds and pushes Skewer Docker images on pushes to main."
  filename    = "deployments/cloudbuild.yaml"

  service_account = google_service_account.cloudbuild.id

  repository_event_config {
    repository = var.cloudbuild_repository_id

    push {
      branch = var.cloudbuild_trigger_branch
    }
  }

  depends_on = [
    google_project_iam_member.cloudbuild_logs_writer,
    google_artifact_registry_repository_iam_member.cloudbuild_ar_writer,
    google_cloud_run_v2_service_iam_member.cloudbuild_run_developer,
    google_service_account_iam_member.cloudbuild_actAs_coordinator,
    google_cloud_run_v2_service_iam_member.cloudbuild_api_developer,
    google_service_account_iam_member.cloudbuild_actAs_api,
  ]
}

output "cloudbuild_service_account_email" {
  description = "Service account email used by the Docker image Cloud Build trigger."
  value       = google_service_account.cloudbuild.email
}

output "cloudbuild_trigger_id" {
  description = "Cloud Build trigger ID for main-branch Docker rebuilds, or null if no repository was configured."
  value       = try(google_cloudbuild_trigger.main_image_rebuild[0].id, null)
}

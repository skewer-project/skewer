resource "google_artifact_registry_repository" "skewer" {
  repository_id = "skewer"
  location      = var.region
  format        = "DOCKER"
  description   = "Docker images for skewer coordinator, render workers, and loom workers"

  depends_on = [google_project_service.apis]
}

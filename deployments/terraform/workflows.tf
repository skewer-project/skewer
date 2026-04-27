resource "google_workflows_workflow" "render_pipeline" {
  name            = "skewer-render-pipeline"
  region          = var.region
  service_account = google_service_account.workflow.email
  description     = "Orchestrates parallel layer rendering and compositing via Cloud Batch"

  source_contents = file("${path.module}/../workflows/render_pipeline.yaml")

  depends_on = [google_project_service.apis]
}

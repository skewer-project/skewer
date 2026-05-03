terraform {
  required_version = ">= 1.6"
  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 5.0"
    }
  }

  backend "gcs" {
    bucket = "skwr-ucd-tfstate"
    prefix = "skewer"
  }
}

provider "google" {
  project               = var.project_id
  region                = var.region
  billing_project       = var.project_id
  user_project_override = true
}

locals {
  name_prefix = "skewer-${var.environment}"
}

# Enable required GCP APIs
resource "google_project_service" "apis" {
  for_each = toset([
    "batch.googleapis.com",
    "cloudbuild.googleapis.com",
    "workflows.googleapis.com",
    "run.googleapis.com",
    "storage.googleapis.com",
    "artifactregistry.googleapis.com",
    "compute.googleapis.com",
    "iam.googleapis.com",
    "iamcredentials.googleapis.com",
    "identitytoolkit.googleapis.com",
    "logging.googleapis.com",
  ])

  service            = each.key
  disable_on_destroy = false
}

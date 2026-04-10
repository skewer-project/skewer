variable "project_id" {
  description = "GCP project ID"
  type        = string
}

variable "region" {
  description = "GCP region for all resources"
  type        = string
  default     = "us-central1"
}

variable "environment" {
  description = "Deployment environment (dev, staging, prod)"
  type        = string
  default     = "dev"
}

variable "batch_machine_type" {
  description = "Machine type for Cloud Batch render/composite jobs"
  type        = string
  default     = "e2-standard-4"
}

variable "data_retention_days" {
  description = "Days to retain render output in the data bucket"
  type        = number
  default     = 30
}

variable "cache_retention_days" {
  description = "Days to retain content-hash layer cache in the cache bucket"
  type        = number
  default     = 90
}

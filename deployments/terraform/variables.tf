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

variable "skewer_batch_machine_type" {
  description = "Machine type for Cloud Batch Skewer render jobs"
  type        = string
  default     = "n2d-highcpu-8"
}

variable "skewer_batch_cpu_milli" {
  description = "vCPU request per Skewer render task in milliCPU"
  type        = number
  default     = 8000
}

variable "skewer_batch_memory_mib" {
  description = "Memory request per Skewer render task in MiB"
  type        = number
  default     = 6144
}

variable "skewer_batch_provisioning_model" {
  description = "Provisioning model for Skewer render jobs"
  type        = string
  default     = "SPOT"

  validation {
    condition     = contains(["STANDARD", "SPOT"], var.skewer_batch_provisioning_model)
    error_message = "skewer_batch_provisioning_model must be STANDARD or SPOT."
  }
}

variable "skewer_batch_max_retry_count" {
  description = "Automatic retry count for transient Skewer render task failures"
  type        = number
  default     = 3
}

variable "loom_batch_machine_type" {
  description = "Machine type for Cloud Batch Loom composite jobs"
  type        = string
  default     = "e2-highmem-8"
}

variable "loom_batch_cpu_milli" {
  description = "vCPU request per Loom composite task in milliCPU"
  type        = number
  default     = 8000
}

variable "loom_batch_memory_mib" {
  description = "Memory request per Loom composite task in MiB"
  type        = number
  default     = 32768
}

variable "loom_batch_provisioning_model" {
  description = "Provisioning model for Loom composite jobs"
  type        = string
  default     = "STANDARD"

  validation {
    condition     = contains(["STANDARD", "SPOT"], var.loom_batch_provisioning_model)
    error_message = "loom_batch_provisioning_model must be STANDARD or SPOT."
  }
}

variable "loom_batch_max_retry_count" {
  description = "Automatic retry count for transient Loom composite task failures"
  type        = number
  default     = 2
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

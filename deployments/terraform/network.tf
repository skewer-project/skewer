resource "google_compute_network" "skewer" {
  name                    = "${local.name_prefix}-vpc"
  auto_create_subnetworks = false

  depends_on = [google_project_service.apis]
}

resource "google_compute_subnetwork" "skewer" {
  name                     = "${local.name_prefix}-subnet"
  network                  = google_compute_network.skewer.id
  region                   = var.region
  ip_cidr_range            = "10.10.0.0/24"
  private_ip_google_access = true  # Allows VMs without public IPs to reach GCP APIs
}

resource "google_compute_router" "skewer" {
  name    = "${local.name_prefix}-router"
  network = google_compute_network.skewer.id
  region  = var.region
}

resource "google_compute_router_nat" "skewer" {
  name   = "${local.name_prefix}-nat"
  router = google_compute_router.skewer.name
  region = var.region

  nat_ip_allocate_option             = "AUTO_ONLY"
  source_subnetwork_ip_ranges_to_nat = "ALL_SUBNETWORKS_ALL_IP_RANGES"
}

# Batch worker VMs only need outbound HTTPS to reach GCS and Artifact Registry
resource "google_compute_firewall" "egress_https" {
  name      = "${local.name_prefix}-allow-egress-https"
  network   = google_compute_network.skewer.id
  direction = "EGRESS"
  priority  = 1000

  allow {
    protocol = "tcp"
    ports    = ["443"]
  }

  target_tags = ["skewer-batch-worker"]
}

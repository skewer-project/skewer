# Identity Platform (Firebase Auth) surface for the previewer sign-in flow.
#
# The previewer uses the Firebase Web SDK to sign in with Google and obtain
# an ID token; skewer-api verifies the token and enforces the email
# allowlist in var.admin_emails.

resource "google_identity_platform_config" "default" {
  project                    = var.project_id
  autodelete_anonymous_users = true

  sign_in {
    allow_duplicate_emails = false

    email {
      enabled           = false
      password_required = false
    }
    anonymous {
      enabled = false
    }
  }

  authorized_domains = var.previewer_authorized_domains

  depends_on = [google_project_service.apis]
}

resource "google_identity_platform_default_supported_idp_config" "google" {
  count = var.google_idp_client_id == null ? 0 : 1

  project       = var.project_id
  enabled       = true
  idp_id        = "google.com"
  client_id     = var.google_idp_client_id
  client_secret = var.google_idp_client_secret

  depends_on = [google_identity_platform_config.default]
}

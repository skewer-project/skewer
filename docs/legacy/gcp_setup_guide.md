# Guide: User-Owned GCP Setup (Bring Your Own Project)

To use Skewer while ensuring you are using your own Google Cloud credits (and not the developer team's), you must configure your own GCP project. This guide explains how to prepare your project so that Skewer can provision workers on your behalf.

---

## 1. Project and Billing Setup
1.  **Create a Project:** In the [GCP Console](https://console.cloud.google.com/), create a new project (e.g., `my-skewer-render`).
2.  **Link Billing:** Ensure your project is linked to **your** Billing Account. Skewer will spin up high-performance VMs; these costs will be charged directly to your account.
3.  **Enable Required APIs:** Skewer needs to manage VMs and storage in your project. Run:
    ```bash
    gcloud services enable compute.googleapis.com \
        container.googleapis.com \
        artifactregistry.googleapis.com \
        storage.googleapis.com
    ```

## 2. Authentication: Providing Skewer Access
Instead of a simple API key, Skewer uses a **Service Account Key** to securely manage resources in your project.

### A. Create the Service Account
1.  Go to **IAM & Admin > Service Accounts**.
2.  Create a service account named `skewer-provisioner`.
3.  Grant it the following roles:
    *   `Compute Instance Admin (v1)`: To spin up/down render workers.
    *   `Storage Admin`: To save and retrieve deep image layers.
    *   `Service Account User`: To allow the workers to run as their own identity.

### B. Generate a JSON Key
1.  Click on the newly created service account.
2.  Go to the **Keys** tab > **Add Key** > **Create new key**.
3.  Select **JSON** and download the file. 
    *   **CRITICAL:** This file grants full control over your project's compute/storage. Keep it safe and never commit it to git.

### C. Link to Skewer
Provide this JSON file to the Skewer CLI when prompted. 

**Note on Security:** The Skewer Coordinator runs **locally** on your computer. Your JSON key is never uploaded to our servers; it is only used by the local Coordinator to communicate directly with Google Cloud APIs on your behalf.

```bash
skewer auth login --key-file=path/to/your-key.json
```

## 3. Worker Image Access
Skewer workers run from a pre-built Docker image. You have two options:
1.  **Public Image (Recommended):** Skewer will pull the official image from the team's public registry.
2.  **Private Registry:** If you want to build your own worker, follow the instructions in `docs/gke_docker_deployment_tutorial.md` to push to your own project's Artifact Registry.

## 4. Storage Configuration (GCS)
Skewer requires a bucket to store intermediate deep layers.
1.  Create a bucket in your project (e.g., `gs://[PROJECT_ID]-skewer-data`).
2.  Ensure your `skewer-provisioner` service account has `Storage Admin` access to this bucket.

## 5. Summary of Costs
By following this guide, you are responsible for:
*   **Compute Engine:** Cost of the VMs while they are rendering.
*   **Cloud Storage:** Cost of storing `.exr` and `.kebab` files.
*   **Networking:** Data egress (if you download results to your local machine).

Skewer will automatically attempt to shut down VMs when jobs are finished to minimize your costs.

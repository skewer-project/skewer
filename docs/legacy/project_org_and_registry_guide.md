# Guide: Project Organization and Image Registries

As you transition from a university project to a tool for public use, your "infrastructure" strategy becomes important for branding, collaboration, and ease of use.

---

## 1. Where to host Docker Images?

Since you are already using Google Cloud (GCP) for GKE and GCS, you have two main options:

### Option A: Google Artifact Registry (Recommended for Workers)
*   **Pros:** Extremely fast deployments to GKE (since it's on the same network), fine-grained security (IAM), and integrated with your GCP billing.
*   **Cons:** Not "discoverable" by the public.
*   **Best Use:** Use this for your **internal components** (the C++ Workers and Go Coordinator) that run your distributed rendering service.

### Option B: Docker Hub (Recommended for Public CLI/Tools)
*   **Pros:** The industry standard for public images. Anyone can `docker pull skewer/cli` without an account.
*   **Cons:** Rate limits for free accounts; separate from your GCP infrastructure.
*   **Best Use:** Use this for the **end-user tools** (like a CLI or a standalone renderer) that you want people to find and use easily.

---

## 2. GitHub Organizations vs. Personal Accounts

For a project intended for the public, **creating a GitHub Organization is highly recommended.**

### Benefits of a GitHub Organization:
1.  **Professionalism:** `github.com/skewer-project/skewer` looks better than `github.com/your-username/skewer`.
2.  **Ownership:** If you collaborate with other students or developers later, you can manage permissions easily without giving them access to your personal account.
3.  **Integration:** You can create an "Organization-wide" Docker Hub account or connect it to other services (like Slack, Discord, or Vercel) under one umbrella.

---

## 3. Public Distribution Strategy

If you want the public to use your tool, you should differentiate between **The Service** and **The Tool**.

### The Tool (Public)
*   **Source:** Public GitHub repo.
*   **Builds:** Use **GitHub Actions** to automatically build and push images to **Docker Hub** whenever you create a "Release" tag.
*   **Registry:** Public Docker Hub (e.g., `docker.io/skewer/renderer`).

### The Service (Internal/Distributed)
*   **Source:** Same repo (or private if preferred).
*   **Builds:** Use **Cloud Build** or GitHub Actions to push images to **Google Artifact Registry**.
*   **Registry:** Private GCP Registry (e.g., `us-central1-docker.pkg.dev/...`).

---

## 4. Setting up a "Project Identity"

If you decide to go "Pro," follow this sequence:

1.  **GitHub:** Create an Organization (e.g., `skewer-rendering`). Move your repo there.
2.  **Docker Hub:** Create an Organization with the same name. This gives you a "namespace" (e.g., `skewer/`).
3.  **GCP:** You don't need a separate "Org" account on GCP unless you want to manage multiple projects, but you should use a **Google Group** for your IAM permissions so you don't have to add individuals one by one.

## 5. Example: GitHub Action for Docker Publishing

Create a file at `.github/workflows/docker-publish.yml` to automate your releases.

```yaml
name: Publish Docker Images

on:
  push:
    tags: [ 'v*.*.*' ] # Triggers on version tags

jobs:
  push_to_registries:
    name: Push Docker image to multiple registries
    runs-on: ubuntu-latest
    steps:
      - name: Check out the repo
        uses: actions/checkout@v3

      - name: Log in to Docker Hub
        uses: docker/login-action@v2
        with:
          username: ${{ secrets.DOCKER_USERNAME }}
          password: ${{ secrets.DOCKER_PASSWORD }}

      - name: Log in to Google Artifact Registry
        uses: google-github-actions/auth@v1
        with:
          credentials_json: ${{ secrets.GCP_SA_KEY }}

      - name: Build and push
        uses: docker/build-push-action@v4
        with:
          context: .
          push: true
          tags: |
            skewer/renderer:latest
            us-central1-docker.pkg.dev/${{ secrets.GCP_PROJECT_ID }}/skewer-repo/worker:latest
```

## Summary Checklist
- [ ] Create a GitHub Organization (e.g., `github.com/skewer-project`).
- [ ] Move the `skewer` repository to the Org.
- [ ] Create a Docker Hub Organization (e.g., `hub.docker.com/u/skewer`).
- [ ] Set up secrets in GitHub (`DOCKER_USERNAME`, `GCP_SA_KEY`, etc.).
- [ ] Enable Google Artifact Registry and create a repository.
- [ ] Add the Docker Publish workflow to your repo.

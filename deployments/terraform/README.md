# Terraform

Each developer should keep Terraform state in their own GCS bucket so local cloud
deployments do not share or overwrite state.

Copy the backend example and edit the bucket name:

```sh
cp backend.example.hcl backend.hcl
```

Use a bucket tied to your project, for example:

```hcl
bucket = "my-project-id-skewer-tfstate"
prefix = "skewer"
```

The bucket must exist before Terraform can initialize the backend. Then run:

```sh
terraform init -backend-config=backend.hcl
```

If this checkout was already initialized against a different backend, use:

```sh
terraform init -reconfigure -backend-config=backend.hcl
```

If you intentionally want to move existing state into your new bucket, use
`-migrate-state` instead of `-reconfigure` and review Terraform's migration
prompt carefully.

Keep `backend.hcl` and `terraform.tfvars` uncommitted. Commit only the shared
Terraform modules and example files.

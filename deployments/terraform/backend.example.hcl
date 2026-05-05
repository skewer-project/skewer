# Copy to backend.hcl and set bucket to your private Terraform state bucket.
# The bucket must already exist before running terraform init.
bucket = "<your-gcp-project-id>-skewer-tfstate"
prefix = "skewer"

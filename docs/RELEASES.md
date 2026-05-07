# Release Builds

The GitHub Actions workflow in `.github/workflows/release.yml` builds ready-to-use Linux x86_64 dynamic module packages.

## Manual Release Only

The workflow runs only through GitHub Actions `workflow_dispatch`. It does not run on pushes, pull requests, or tag pushes.

Open the workflow in GitHub Actions and choose:

- `nginx_versions`: space-separated versions, for example `1.24.0 1.26.3 1.28.0`
- `version_bump`: `patch`, `minor`, or `major`
- `push_image`: whether to push the Docker image to GHCR

The workflow fetches existing `vMAJOR.MINOR.PATCH` tags, computes the next semantic version, creates an annotated tag, pushes it, and publishes the GitHub release.

Examples:

```text
latest v1.2.3 + patch = v1.2.4
latest v1.2.3 + minor = v1.3.0
latest v1.2.3 + major = v2.0.0
no existing tag + patch = v0.0.1
```

## Built Nginx Versions

By default, the workflow builds packages for:

- Nginx 1.24.0
- Nginx 1.26.3
- Nginx 1.28.0

Each release asset is named like:

```text
ngx_http_monitoring_module-1.0.0-v1.0.0-nginx-1.28.0-linux-x86_64.tar.gz
```

Each package contains:

- `modules/ngx_http_monitoring_module.so`
- `INSTALL.md`
- `METADATA.txt`
- `SHA256SUMS`
- example config
- API/config/performance docs

## Docker Image

When `push_image` is true, the workflow also builds and pushes:

```text
ghcr.io/<owner>/<repo>:<tag>
ghcr.io/<owner>/<repo>:latest
```

## Compatibility Warning

Nginx dynamic modules are ABI-sensitive. The release packages are built with `--with-compat` on Ubuntu 22.04 against the Nginx version in the filename. Use the matching Nginx version and a compatible Linux/glibc runtime. Rebuild locally when using a vendor Nginx package with materially different module ABI or hardening options.

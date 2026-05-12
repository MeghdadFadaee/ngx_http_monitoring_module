# Release Builds

The GitHub Actions workflow in `.github/workflows/release.yml` builds ready-to-use Linux dynamic module packages for x86_64 and ARM targets. Release binaries are built in Ubuntu Bionic 18.04 containers, giving the module artifacts a `ubuntu-bionic-glibc-2.27` libc baseline.

## Manual Release Only

The workflow runs only through GitHub Actions `workflow_dispatch`. It does not run on pushes, pull requests, or tag pushes.

Open the workflow in GitHub Actions and choose:

- `nginx_versions`: space-separated versions, for example `1.24.0 1.26.3 1.28.0`
- `target_arches`: space-separated targets, default `linux-x86_64 linux-arm64 linux-armv7`
- `version_bump`: `patch`, `minor`, or `major`
- `push_image`: whether to push the multi-platform Docker image to GHCR

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

- Nginx 1.22.1
- Nginx 1.24.0
- Nginx 1.26.3
- Nginx 1.28.0
- Nginx 1.30.0

## Built CPU Targets

By default, the workflow builds module packages for:

- `linux-x86_64`
- `linux-arm64`
- `linux-armv7`

Aliases such as `amd64`, `arm64`, `aarch64`, `armv7`, and `armhf` are normalized by the workflow.

Each release asset is named like:

```text
ngx_http_monitoring_module-v1.0.0-nginx-1.28.0-linux-x86_64.tar.gz
ngx_http_monitoring_module-v1.0.0-nginx-1.28.0-linux-arm64.tar.gz
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

The image is built with Docker Buildx for the same normalized target platforms requested by `target_arches`.

The release image build passes:

```text
BASE_IMAGE=ubuntu:18.04
OPENSSL_RUNTIME_PACKAGE=libssl1.1
```

## Compatibility Warning

Nginx dynamic modules are ABI-sensitive. The release packages are built with `--with-compat` on Ubuntu Bionic 18.04 against the Nginx version and CPU architecture in the filename. Use the matching Nginx version, CPU architecture, and a Linux/glibc runtime compatible with `ubuntu-bionic-glibc-2.27`. Rebuild locally when using a vendor Nginx package with materially different module ABI or hardening options.

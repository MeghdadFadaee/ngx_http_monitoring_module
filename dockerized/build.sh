#!/usr/bin/env sh
set -eu

IMAGE="${IMAGE:-ngx-http-monitoring-module:local}"
NGINX_VERSION="${NGINX_VERSION:-1.28.0}"
BASE_IMAGE="${BASE_IMAGE:-debian:bookworm-slim}"
OPENSSL_RUNTIME_PACKAGE="${OPENSSL_RUNTIME_PACKAGE:-libssl3}"

cd "$(dirname "$0")/.."

docker build \
    --build-arg "NGINX_VERSION=$NGINX_VERSION" \
    --build-arg "BASE_IMAGE=$BASE_IMAGE" \
    --build-arg "OPENSSL_RUNTIME_PACKAGE=$OPENSSL_RUNTIME_PACKAGE" \
    -f dockerized/Dockerfile \
    -t "$IMAGE" \
    .

echo "Built $IMAGE"

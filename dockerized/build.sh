#!/usr/bin/env sh
set -eu

IMAGE="${IMAGE:-ngx-http-monitoring-module:local}"
NGINX_VERSION="${NGINX_VERSION:-1.28.0}"

cd "$(dirname "$0")/.."

docker build \
    --build-arg "NGINX_VERSION=$NGINX_VERSION" \
    -f dockerized/Dockerfile \
    -t "$IMAGE" \
    .

echo "Built $IMAGE"

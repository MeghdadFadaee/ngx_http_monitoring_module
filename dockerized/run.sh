#!/usr/bin/env sh
set -eu

IMAGE="${IMAGE:-ngx-http-monitoring-module:local}"
NAME="${NAME:-ngx-http-monitoring-module}"
PORT="${PORT:-8080}"

docker rm -f "$NAME" >/dev/null 2>&1 || true

docker run \
    --name "$NAME" \
    -p "$PORT:8080" \
    "$IMAGE"

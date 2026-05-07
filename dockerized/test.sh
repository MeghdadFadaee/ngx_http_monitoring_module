#!/usr/bin/env sh
set -eu

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"

curl -fsS "$BASE_URL/" >/dev/null
curl -fsS "$BASE_URL/monitor" >/dev/null
curl -fsS "$BASE_URL/monitor/api" >/dev/null
curl -fsS "$BASE_URL/monitor/api/system" >/dev/null
curl -fsS "$BASE_URL/monitor/api/nginx" >/dev/null
curl -fsS "$BASE_URL/monitor/api/network" >/dev/null
curl -fsS "$BASE_URL/monitor/api/disk" >/dev/null
curl -fsS "$BASE_URL/monitor/api/processes" >/dev/null
curl -fsS "$BASE_URL/monitor/api/upstreams" >/dev/null
curl -fsS "$BASE_URL/monitor/api/connections" >/dev/null
curl -fsS "$BASE_URL/monitor/api/requests" >/dev/null
curl -fsS "$BASE_URL/monitor/metrics" >/dev/null
curl -fsS "$BASE_URL/monitor/health" >/dev/null

timeout 3 curl -fsS -N "$BASE_URL/monitor/live" >/dev/null || true

echo "Dockerized module endpoints are reachable at $BASE_URL"

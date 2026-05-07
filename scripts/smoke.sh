#!/usr/bin/env sh
set -eu

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
TOKEN="${TOKEN:-}"

auth_arg=""
if [ "$TOKEN" != "" ]; then
    auth_arg="?token=$TOKEN"
fi

curl -fsS "$BASE_URL/monitor" >/dev/null
curl -fsS "$BASE_URL/monitor/api$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/system$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/nginx$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/network$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/disk$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/processes$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/upstreams$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/connections$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/api/requests$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/metrics$auth_arg" >/dev/null
curl -fsS "$BASE_URL/monitor/health$auth_arg" >/dev/null

timeout 3 curl -fsS -N "$BASE_URL/monitor/live$auth_arg" >/dev/null || true
echo "smoke checks completed"

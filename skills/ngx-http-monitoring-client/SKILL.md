---
name: ngx-http-monitoring-client
description: Use this skill when connecting clients or agents to an ngx_http_monitoring_module service, querying its JSON/SSE/Prometheus endpoints, debugging access problems, or integrating dashboards/health checks. Supports deployments protected by Nginx Basic Auth and/or monitor_api_token.
---

# ngx_http_monitoring_module Client

Use this skill to consume a deployed `ngx_http_monitoring_module` service from scripts, agents, dashboards, health checks, CI, or observability clients.

## Inputs To Determine

Before calling the service, identify:

- `base_url`: root URL that serves `/monitor`, for example `http://127.0.0.1:8080`
- optional API token: sent as `X-Monitor-Token`, `Authorization: Bearer`, or `?token=...`
- optional Nginx Basic Auth credentials: `username:password`
- target endpoint: dashboard, JSON API, SSE, Prometheus, or health

Never invent credentials. Ask the user, read documented deployment config, or use provided environment variables.

Recommended environment variables:

```sh
MONITOR_BASE_URL=http://127.0.0.1:8080
MONITOR_TOKEN=change-me
MONITOR_BASIC_AUTH=user:password
```

## Endpoints

- `GET /monitor` - embedded HTML dashboard
- `GET /monitor/api` - full JSON metrics
- `GET /monitor/api/system` - CPU, load, memory, swap, uptime
- `GET /monitor/api/nginx` - Nginx connection/request/worker metrics
- `GET /monitor/api/network` - network interfaces and counters
- `GET /monitor/api/disk` - disk and filesystem metrics
- `GET /monitor/api/processes` - process, TCP, socket, and worker stats
- `GET /monitor/api/upstreams` - observed upstream/backend stats
- `GET /monitor/api/connections` - connection/SSE/keepalive counters
- `GET /monitor/api/requests` - request counters, histograms, percentiles, top URLs
- `GET /monitor/live` - Server-Sent Events stream
- `GET /monitor/metrics` - Prometheus text format
- `GET /monitor/health` - lightweight JSON health check

## Auth Rules

If the service is behind Nginx Basic Auth, include Basic Auth on every request, including `/monitor`, `/monitor/live`, and `/monitor/metrics`.

If `monitor_api_token` is configured, include the token on non-dashboard endpoints. Prefer a header over query string unless the client cannot set headers.

Preferred headers:

```sh
-H "X-Monitor-Token: $MONITOR_TOKEN"
```

Alternative bearer header:

```sh
-H "Authorization: Bearer $MONITOR_TOKEN"
```

Basic Auth with curl:

```sh
-u "$MONITOR_BASIC_AUTH"
```

When both Basic Auth and API token are enabled, send both.

Do not print secrets in final answers, logs, or copied command output. Redact with `<redacted>` when summarizing.

## Curl Patterns

Build arguments conditionally:

```sh
base="${MONITOR_BASE_URL:-http://127.0.0.1:8080}"
auth_args=""
token_args=""

if [ "${MONITOR_BASIC_AUTH:-}" != "" ]; then
  auth_args="-u $MONITOR_BASIC_AUTH"
fi

if [ "${MONITOR_TOKEN:-}" != "" ]; then
  token_args="-H X-Monitor-Token: $MONITOR_TOKEN"
fi
```

For exact shell execution, prefer explicit commands instead of string-expanded credentials when possible:

```sh
curl -fsS -u "$MONITOR_BASIC_AUTH" -H "X-Monitor-Token: $MONITOR_TOKEN" \
  "$MONITOR_BASE_URL/monitor/api"
```

Without token:

```sh
curl -fsS -u "$MONITOR_BASIC_AUTH" "$MONITOR_BASE_URL/monitor/api"
```

Without Basic Auth:

```sh
curl -fsS -H "X-Monitor-Token: $MONITOR_TOKEN" "$MONITOR_BASE_URL/monitor/api"
```

Health check:

```sh
curl -fsS -u "$MONITOR_BASIC_AUTH" -H "X-Monitor-Token: $MONITOR_TOKEN" \
  "$MONITOR_BASE_URL/monitor/health"
```

Prometheus:

```sh
curl -fsS -u "$MONITOR_BASIC_AUTH" -H "X-Monitor-Token: $MONITOR_TOKEN" \
  "$MONITOR_BASE_URL/monitor/metrics"
```

SSE sample:

```sh
curl -fsS -N -u "$MONITOR_BASIC_AUTH" -H "X-Monitor-Token: $MONITOR_TOKEN" \
  "$MONITOR_BASE_URL/monitor/live"
```

## Browser And Dashboard

Open:

```text
<base_url>/monitor
```

If the API token is required and the browser cannot set headers for `EventSource`, append the token:

```text
<base_url>/monitor?token=<token>
```

The dashboard forwards the `token` query parameter to API and SSE calls. Basic Auth is handled by the browser after the Nginx auth challenge.

## JavaScript Client

Use headers for JSON polling:

```js
const baseUrl = process.env.MONITOR_BASE_URL ?? "http://127.0.0.1:8080";
const token = process.env.MONITOR_TOKEN;

const headers = token ? { "X-Monitor-Token": token } : {};
const res = await fetch(`${baseUrl}/monitor/api`, { headers });
if (!res.ok) throw new Error(`monitor api failed: ${res.status}`);
const metrics = await res.json();
```

For browser `EventSource`, headers are not available. Use query token if needed:

```js
const url = new URL("/monitor/live", baseUrl);
if (token) url.searchParams.set("token", token);
const events = new EventSource(url);
events.addEventListener("metrics", (event) => {
  const metrics = JSON.parse(event.data);
});
```

If Basic Auth is required in browser clients, rely on the browser's authenticated session. Do not embed `user:password` in URLs.

## Interpreting Responses

Every JSON response includes:

- `version`
- `module`
- `timestamp`
- `scope`

Common fields:

- CPU usage: `system.cpu.usage`
- memory used percent: `system.memory.used_pct`
- request rate: `requests.requests_per_sec` or `nginx.requests.requests_per_sec`
- latency: `requests.latency.p50`, `p90`, `p95`, `p99`
- errors: `requests.status["4xx"]`, `requests.status["5xx"]`
- active connections: `connections.active` or `nginx.connections.active`

Treat missing fields as disabled collectors, unsupported Nginx build options, or insufficient traffic. For exact active/reading/writing/waiting connection counters, the Nginx build must include `--with-http_stub_status_module`.

## Troubleshooting

- `401`: Basic Auth or API token is missing or wrong. If both are configured, send both.
- `403`: `monitor_allow` / `monitor_deny` blocked the client IP.
- `404`: `monitor on`, `monitor_api on`, or `monitor_sse on` may not be enabled in the matching location.
- `429`: `monitor_rate_limit` was exceeded.
- Empty connection counters: Nginx may not have been built with stub status support.
- SSE connects then drops: proxy buffering or idle timeout may be in front of the service. Set `proxy_buffering off` and allow long-lived responses.

## Security Defaults

When integrating with another client:

- prefer HTTPS
- prefer header token over query token
- avoid logging headers, URLs containing `token=`, or Basic Auth strings
- use least-privilege network ACLs in front of `/monitor`
- avoid embedding Basic Auth credentials in browser source

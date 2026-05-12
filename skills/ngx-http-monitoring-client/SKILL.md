---
name: ngx-http-monitoring-client
description: Use this skill when connecting clients or agents to an ngx_http_monitoring_module service, querying its JSON/SSE/Prometheus endpoints, reading API response structures, debugging access problems, or integrating dashboards/health checks. Supports deployments protected by Nginx Basic Auth and/or monitor_api_token.
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

## API Structures

All JSON API responses are compact, versioned, and timestamped. Treat unknown fields as forward-compatible additions, and treat missing sections as disabled collectors, unsupported Nginx build options, or unavailable Linux `/proc` data.

Common envelope:

```json
{
  "version": 1,
  "module": "1.0.0",
  "timestamp": 1710000000,
  "msec": 123,
  "scope": "full",
  "pid": 12345
}
```

Endpoint-specific API routes such as `/monitor/api/system` return the common envelope plus one top-level field matching the route. The examples below show that section's value unless explicitly shown as a complete response.

`GET /monitor/api` returns the envelope plus:

```json
{
  "system": {},
  "nginx": {},
  "network": {},
  "disk": {},
  "processes": {},
  "upstreams": [],
  "connections": {},
  "requests": {},
  "history": []
}
```

`GET /monitor/api/system`:

```json
{
  "cpu": {
    "usage": 12.5,
    "cores": 8,
    "load": [0.12, 0.20, 0.30]
  },
  "memory": {
    "total": 16777216,
    "available": 8388608,
    "free": 4194304,
    "used": 8388608,
    "used_pct": 50.0
  },
  "swap": {
    "total": 2097152,
    "free": 1048576,
    "used_pct": 50.0
  },
  "uptime": 123456
}
```

`GET /monitor/api/nginx`:

```json
{
  "connections": {
    "accepted": 1000,
    "handled": 1000,
    "active": 20,
    "reading": 1,
    "writing": 4,
    "waiting": 15
  },
  "requests": {
    "total": 50000,
    "responses": 49990,
    "requests_per_sec": 125.5,
    "responses_per_sec": 125.3
  },
  "ssl": {
    "requests": 100,
    "handshakes": 20
  },
  "keepalive": {
    "requests": 250
  },
  "workers": []
}
```

`GET /monitor/api/network`:

```json
{
  "rx_bytes": 123456789,
  "tx_bytes": 987654321,
  "rx_packets": 10000,
  "tx_packets": 12000,
  "rx_errors": 0,
  "tx_errors": 0,
  "interfaces": [
    {
      "name": "eth0",
      "rx_bytes": 123456789,
      "tx_bytes": 987654321,
      "rx_packets": 10000,
      "tx_packets": 12000,
      "rx_errors": 0,
      "tx_errors": 0,
      "up": true
    }
  ]
}
```

`GET /monitor/api/disk`:

```json
{
  "reads": 1000,
  "writes": 2000,
  "read_bytes": 4096000,
  "write_bytes": 8192000,
  "devices": [
    {
      "name": "sda",
      "reads": 1000,
      "writes": 2000,
      "read_bytes": 4096000,
      "write_bytes": 8192000,
      "io_ms": 120
    }
  ],
  "filesystems": [
    {
      "path": "/",
      "type": "ext4",
      "total": 107374182400,
      "used": 53687091200,
      "free": 53687091200,
      "avail": 53687091200,
      "files": 1000000,
      "files_free": 750000
    }
  ]
}
```

`GET /monitor/api/processes`:

```json
{
  "process_count": 140,
  "tcp": {
    "established": 20,
    "listen": 8
  },
  "sockets": {
    "used": 512,
    "tcp": 120,
    "udp": 16
  },
  "workers": []
}
```

`GET /monitor/api/connections`:

```json
{
  "accepted": 1000,
  "handled": 1000,
  "active": 20,
  "reading": 1,
  "writing": 4,
  "waiting": 15,
  "ssl_requests": 100,
  "ssl_handshakes": 20,
  "keepalive_requests": 250,
  "sse_clients": 2,
  "sse_events": 500,
  "rate_limited": 0
}
```

`GET /monitor/api/requests`:

```json
{
  "total": 50000,
  "responses": 49990,
  "bytes": 104857600,
  "requests_per_sec": 125.5,
  "responses_per_sec": 125.3,
  "error_rate": 0.0078,
  "latency": {
    "avg": 8.4,
    "p50": 4,
    "p90": 15,
    "p95": 22,
    "p99": 80
  },
  "status": {
    "1xx": 0,
    "2xx": 49000,
    "3xx": 600,
    "4xx": 300,
    "5xx": 90
  },
  "methods": {
    "GET": 45000,
    "POST": 4000,
    "PUT": 100,
    "DELETE": 50,
    "HEAD": 500,
    "OPTIONS": 200,
    "PATCH": 50,
    "OTHER": 100
  },
  "latency_histogram": [
    {
      "le": 1,
      "count": 100
    },
    {
      "le": "+Inf",
      "count": 50000
    }
  ],
  "size_histogram": [
    {
      "le": 512,
      "count": 10000
    },
    {
      "le": "+Inf",
      "count": 50000
    }
  ],
  "top_urls": [],
  "user_agents": []
}
```

`GET /monitor/api/upstreams`:

```json
[
  {
    "peer": "127.0.0.1:9000",
    "requests": 1000,
    "failures": 2,
    "status_4xx": 10,
    "status_5xx": 2,
    "avg_latency": 12.5,
    "last_seen": 1710000000
  }
]
```

History samples in `GET /monitor/api`:

```json
[
  {
    "timestamp": 1710000000,
    "cpu": 12.5,
    "memory": 50.0,
    "swap": 0.0,
    "rps": 125.5,
    "responses_per_sec": 125.3,
    "network_rx_per_sec": 1024,
    "network_tx_per_sec": 2048,
    "disk_read_per_sec": 0,
    "disk_write_per_sec": 4096,
    "latency_p95": 22,
    "requests_total": 50000,
    "status_4xx": 300,
    "status_5xx": 90
  }
]
```

Shared nested structures:

```json
{
  "worker": {
    "slot": 0,
    "pid": 12345,
    "active": true,
    "requests": 10000,
    "bytes": 10485760,
    "errors": 3,
    "vm_size": 102400,
    "vm_rss": 20480,
    "voluntary_ctxt": 100,
    "nonvoluntary_ctxt": 5,
    "last_seen": 1710000000
  },
  "top_url": {
    "url": "/api/orders",
    "hits": 1000,
    "errors": 3,
    "bytes": 1048576,
    "avg_latency": 7.8,
    "last_seen": 1710000000
  },
  "user_agent": {
    "user_agent": "curl/8.0.1",
    "hits": 100,
    "errors": 0,
    "bytes": 20480,
    "avg_latency": 2.1,
    "last_seen": 1710000000
  }
}
```

SSE `GET /monitor/live` sends `metrics` events whose `data` payload has this structure:

```json
{
  "version": 1,
  "timestamp": 1710000000,
  "sequence": 42,
  "system": {
    "cpu": {
      "usage": 12.5,
      "cores": 8
    },
    "memory": {
      "used_pct": 50.0,
      "total": 16777216,
      "available": 8388608
    }
  },
  "requests": {
    "total": 50000,
    "requests_per_sec": 125.5,
    "responses_per_sec": 125.3,
    "latency": {
      "p95": 22,
      "p99": 80
    },
    "status": {
      "4xx": 300,
      "5xx": 90
    }
  },
  "connections": {
    "active": 20,
    "reading": 1,
    "writing": 4,
    "waiting": 15,
    "sse_clients": 2,
    "keepalive_requests": 250
  },
  "network": {
    "rx_bytes": 123456789,
    "tx_bytes": 987654321
  },
  "disk": {
    "read_bytes": 4096000,
    "write_bytes": 8192000
  }
}
```

`GET /monitor/metrics` exposes Prometheus text metrics including:

```text
nginx_monitor_requests_total
nginx_monitor_active_connections
nginx_monitor_cpu_usage_ratio
nginx_monitor_memory_used_ratio
nginx_monitor_latency_p95_ms
```

`GET /monitor/health` returns a complete health response:

```json
{
  "version": 1,
  "module": "1.0.0",
  "timestamp": 1710000000,
  "msec": 123,
  "scope": "health",
  "pid": 12345,
  "status": "ok",
  "generation": 100,
  "sse_clients": 2
}
```

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

# API

All JSON responses are compact, versioned, and timestamped.

```json
{
  "version": 1,
  "module": "1.0.0",
  "timestamp": 1710000000,
  "scope": "full"
}
```

## Authentication

If `monitor_api_token` is set, non-dashboard endpoints require one of:

- `Authorization: Bearer <token>`
- `X-Monitor-Token: <token>`
- `?token=<token>`

If `monitor_basic_auth` is set to `user:password`, every monitoring endpoint requires HTTP Basic auth. For public production deployments, prefer terminating authentication at Nginx with `auth_basic` and `auth_basic_user_file`.

## JSON Endpoints

### `/monitor/api`

Returns all sections:

- `system`
- `nginx`
- `network`
- `disk`
- `processes`
- `upstreams`
- `connections`
- `requests`
- `history`

### `/monitor/api/system`

Includes:

- `cpu.usage`
- `cpu.cores`
- `cpu.load`
- `memory.total`
- `memory.available`
- `memory.used`
- `memory.used_pct`
- `swap.total`
- `swap.free`
- `swap.used_pct`
- `uptime`

### `/monitor/api/requests`

Includes:

- total request and response counters
- request and response moving averages
- status counters
- method distribution
- response size histogram
- latency histogram
- latency percentiles `p50`, `p90`, `p95`, `p99`
- `top_urls`
- `user_agents`

### `/monitor/live`

SSE stream:

```text
retry: 3000
: heartbeat 42
id: 42
event: metrics
data: {"version":1,"timestamp":1710000000,...}
```

The dashboard uses SSE first and falls back to polling `/monitor/api`.

### `/monitor/metrics`

Prometheus text format with core counters and gauges:

- `nginx_monitor_requests_total`
- `nginx_monitor_active_connections`
- `nginx_monitor_cpu_usage_ratio`
- `nginx_monitor_memory_used_ratio`
- `nginx_monitor_latency_p95_ms`

# ngx_http_monitoring_module

`ngx_http_monitoring_module` is a Linux-only dynamic Nginx HTTP module for live server monitoring, JSON APIs, Server-Sent Events, and an embedded dashboard.

## Features

- Dynamic module build for Nginx 1.24+
- Shared-memory metrics across workers
- Atomic request counters and bounded top-N tables
- Timer-based `/proc` collectors with cached snapshots
- JSON REST API under `/monitor/api`
- Embedded dark dashboard at `/monitor`
- Server-Sent Events stream at `/monitor/live`
- Prometheus text endpoint at `/monitor/metrics`
- ACL, optional basic auth, optional API token, CORS, and simple global API rate limiting
- Historical ring buffer with configurable retention and resolution

## Build

Install Nginx build prerequisites and use an Nginx source tree configured similarly to the Nginx binary you will load the module into.

```sh
NGINX_SRC=/usr/local/src/nginx-1.24.0 make module
```

Recommended configure options:

```sh
NGINX_CONFIGURE_ARGS="--with-compat --with-http_ssl_module --with-http_stub_status_module" \
NGINX_SRC=/usr/local/src/nginx-1.24.0 make module
```

The compiled module is written to:

```text
build/ngx_http_monitoring_module.so
```

## Docker Image

A Dockerized Nginx image is available in [dockerized](</dockerized>):

```sh
docker build -f dockerized/Dockerfile -t ngx-http-monitoring-module:local .
docker run --rm -p 8080:8080 ngx-http-monitoring-module:local
```

Then open:

```text
http://127.0.0.1:8080/monitor
```

## Agent Skill

A reusable Codex skill for clients and agents is available at `skills/ngx-http-monitoring-client/SKILL.md`. It covers JSON, SSE, Prometheus, API token usage, and Nginx Basic Auth.

## GitHub Releases

The repository includes a manual GitHub Actions workflow at `.github/workflows/release.yml` that computes the next `vMAJOR.MINOR.PATCH` tag, builds Linux x86_64 dynamic module tarballs, pushes the tag, and publishes the release. See `docs/RELEASES.md` for release asset format and compatibility notes.

## Load The Module

Load it from `nginx.conf`:

```nginx
load_module modules/ngx_http_monitoring_module.so;
```

## Minimal Configuration

```nginx
http {
    monitor_refresh_interval 1s;
    monitor_history 5m;
    monitor_resolution 1s;

    server {
        listen 8080;

        location /monitor {
            monitor on;
            monitor_allow 127.0.0.1/32;
            monitor_deny all;
        }
    }
}
```

Open:

```text
http://127.0.0.1:8080/monitor
```

After Nginx is running, run endpoint smoke checks:

```sh
BASE_URL=http://127.0.0.1:8080 sh scripts/smoke.sh
```

## Endpoints

- `GET /monitor` - embedded dashboard
- `GET /monitor/api` - full JSON document
- `GET /monitor/api/system` - CPU, load, memory, swap, uptime
- `GET /monitor/api/nginx` - Nginx connection/request/worker metrics
- `GET /monitor/api/network` - interfaces and traffic counters
- `GET /monitor/api/disk` - block device and filesystem counters
- `GET /monitor/api/processes` - process count, TCP/socket stats, workers
- `GET /monitor/api/upstreams` - observed upstream peer stats
- `GET /monitor/api/connections` - connection and SSE counters
- `GET /monitor/api/requests` - status, methods, histograms, top URLs, user agents
- `GET /monitor/live` - SSE metrics stream
- `GET /monitor/metrics` - Prometheus-compatible text metrics
- `GET /monitor/health` - lightweight health JSON

## Notes

The module is intentionally dependency-free at runtime. System data is collected by worker timers from Linux `/proc`, `statvfs()`, and `getifaddrs()`, then served from shared memory. API requests never parse `/proc` directly.

For exact active/reading/writing/waiting connection counters, build Nginx with `--with-http_stub_status_module`.

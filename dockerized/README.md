# Dockerized Nginx Image

This folder builds an Nginx image with `ngx_http_monitoring_module` compiled and loaded as a dynamic module.

## Build

From the repository root:

```sh
docker build -f dockerized/Dockerfile -t ngx-http-monitoring-module:local .
```

Or:

```sh
sh dockerized/build.sh
```

To choose a different Nginx version:

```sh
NGINX_VERSION=1.28.0 sh dockerized/build.sh
```

## Run

```sh
docker run --rm -p 8080:8080 ngx-http-monitoring-module:local
```

Open:

```text
http://127.0.0.1:8080/monitor
```

## Docker Compose

```sh
cd dockerized
docker compose up --build
```

## Smoke Test

With the container running:

```sh
sh dockerized/test.sh
```

## Image Layout

- Nginx binary: `/usr/local/sbin/nginx`
- Module: `/usr/local/nginx/modules/ngx_http_monitoring_module.so`
- Config: `/etc/nginx/nginx.conf`
- Listen port: `8080`

The supplied `nginx.conf` enables:

- `/monitor`
- `/monitor/api`
- `/monitor/live`
- `/monitor/metrics`
- `/monitor/health`

This demo config allows access from any client. Restrict `location /monitor` before exposing the container outside a trusted network.

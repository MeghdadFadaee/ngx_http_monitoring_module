# Architecture

## Request Path

The module registers two HTTP phase handlers:

- content phase: handles `/monitor`, `/monitor/api*`, `/monitor/live`, `/monitor/metrics`, and `/monitor/health`
- log phase: accounts completed application requests

Monitoring endpoints are excluded from request accounting to avoid dashboard polling skewing the service metrics.

## Shared Memory

The module allocates one shared memory zone named `ngx_http_monitoring`. The zone contains:

- global request, response, connection, and system counters
- fixed-size top URL and user-agent tables
- fixed-size upstream peer table
- per-worker metric slots keyed by `ngx_process_slot`
- fixed-size historical ring buffer
- collector lock and API rate-limit counters

Hot request-path updates use Nginx atomic operations. The shared slab mutex is used only for rare top-N slot creation or replacement and rate-limit window resets.

## Collection

Each worker owns a timer, but collection is guarded by an atomic shared lock, so only one worker performs a collection pass at a time. Collection reads:

- `/proc/stat`
- `/proc/loadavg`
- `/proc/meminfo`
- `/proc/net/dev`
- `/proc/diskstats`
- `/proc/uptime`
- `/proc/net/tcp`
- `/proc/net/tcp6`
- `/proc/net/sockstat`
- `/proc/mounts`
- `/proc/self/status`
- `/proc`
- `statvfs()`
- `getifaddrs()`

The collector computes deltas and moving averages, then appends a sample to the ring buffer according to `monitor_resolution`.

## Serialization

JSON and Prometheus responses are generated from shared-memory snapshots only. API handlers do not read `/proc`, do not allocate persistent state, and use one request-pool buffer per response.

## SSE

`/monitor/live` is a long-lived request. Each client owns two reusable output buffers and a timer. The writer skips a tick if the connection still has pending buffered output, preventing slow clients from accumulating unbounded memory.

## Upstreams

Upstream metrics are passively observed from Nginx upstream state recorded on proxied requests. This avoids active backend probing in workers. Failures are counted from zero or `5xx` upstream statuses.

## Compatibility

The module is designed as an Nginx dynamic HTTP module and should be built with `--with-compat` against the target Nginx source tree. Stub-status counters are compiled in when the target Nginx build includes `--with-http_stub_status_module`; otherwise request counters still work but low-level active/reading/writing/waiting counters remain zero.

Collectors avoid glibc-only helpers such as `getmntent()` and parse `/proc/mounts` directly. This keeps filesystem collection compatible with libc implementations that do not ship `<mntent.h>`, while still requiring Linux `/proc`, `statvfs()`, and `getifaddrs()`.

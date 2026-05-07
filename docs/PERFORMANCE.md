# Performance

## Hot Path

The log-phase request accounting path performs:

- one timestamp delta calculation
- atomic increments for request, status, method, latency, and size buckets
- one bounded top-URL table update
- optional user-agent and upstream updates

Top-N table insertion or replacement takes the shared slab mutex, but existing entries update with atomics only.

## Collector Cost

Collectors run from Nginx timers at `monitor_refresh_interval`. API requests do not trigger collection. Default collection is once per second.

For very large hosts, `/proc/net/tcp` and `/proc/net/tcp6` can be expensive because they scale with connection count. If needed, increase `monitor_refresh_interval` or disable system collection:

```nginx
monitor_collect_system off;
```

## Memory

Default shared memory is 8 MiB:

```nginx
monitor_shm_size 8m;
```

The default fixed-size structures are bounded:

- history samples: 3600 maximum
- top URLs: 256 maximum
- user agents: 64 maximum
- upstream peers: 128 maximum
- workers: 128 maximum
- interfaces, disks, filesystems: 32 each

## Operational Guidance

- Build with `--with-compat`.
- Build with `--with-http_stub_status_module` for exact Nginx connection counters.
- Keep dashboard access private with `monitor_allow` / `monitor_deny` or standard Nginx auth modules.
- Use `monitor_api_token` when exposing API or SSE across trust boundaries.
- Put `/monitor` behind an internal listener or VPN for production.
- Keep `monitor_resolution` at `1s` or higher on very busy systems.

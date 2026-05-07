# Configuration

## Core

```nginx
monitor on | off;
monitor_dashboard on | off;
monitor_api on | off;
monitor_sse on | off;
```

`monitor` can be used at `http`, `server`, or `location` scope. A prefix location is recommended:

```nginx
location /monitor {
    monitor on;
}
```

## Collection

```nginx
monitor_refresh_interval 1s;
monitor_history 5m;
monitor_resolution 1s;
monitor_shm_size 8m;
monitor_collect_system on;
monitor_collect_nginx on;
monitor_collect_network on;
monitor_access_log on;
monitor_max_top_urls 100;
```

`monitor_history / monitor_resolution` is capped by the compiled maximum of 3600 samples.

## Security

```nginx
monitor_allow 10.0.0.0/8;
monitor_allow 127.0.0.1/32;
monitor_deny all;

monitor_basic_auth off;
monitor_api_token "change-me";
monitor_rate_limit 120;
monitor_cors off;
```

ACL rules are evaluated in order. `monitor_basic_auth` accepts `off` or a literal `user:password`. For strong password storage, use Nginx `auth_basic` in the same location instead.

`monitor_cors` accepts `off`, `*`, or a literal origin such as `https://ops.example.com`.

#include "ngx_http_monitoring.h"

#include <net/if.h>
#include <stdarg.h>

typedef struct {
    u_char    *pos;
    u_char    *last;
    ngx_int_t  failed;
} ngx_http_monitoring_json_writer_t;

typedef struct {
    u_char              key[NGX_HTTP_MONITORING_KEY_LEN];
    ngx_atomic_uint_t   hits;
    ngx_atomic_uint_t   errors;
    ngx_atomic_uint_t   bytes;
    ngx_atomic_uint_t   latency_ms_total;
    ngx_atomic_uint_t   last_seen;
} ngx_http_monitoring_top_view_t;

static void ngx_http_monitoring_json_append(
    ngx_http_monitoring_json_writer_t *jw, const char *s);
static void ngx_http_monitoring_json_printf(
    ngx_http_monitoring_json_writer_t *jw, const char *fmt, ...);
static void ngx_http_monitoring_json_string(
    ngx_http_monitoring_json_writer_t *jw, const u_char *data, size_t len);
static void ngx_http_monitoring_json_header(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_request_t *r,
    const char *scope);
static void ngx_http_monitoring_json_system(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_network(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_disk(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_processes(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_nginx(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_connections(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_requests(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_request_t *r,
    ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_upstreams(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_history(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_workers(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_json_top_entries(
    ngx_http_monitoring_json_writer_t *jw, ngx_http_request_t *r,
    ngx_http_monitoring_top_entry_t *entries, ngx_uint_t capacity,
    const char *key_name);
static ngx_int_t ngx_http_monitoring_send_buffer(ngx_http_request_t *r,
    ngx_buf_t *b, ngx_str_t *type);
static void ngx_http_monitoring_add_no_store(ngx_http_request_t *r);
static void ngx_http_monitoring_sort_top(ngx_http_monitoring_top_view_t *v,
    ngx_uint_t n);

static ngx_uint_t ngx_http_monitoring_latency_bounds[
    NGX_HTTP_MONITORING_LATENCY_BUCKETS - 1
] = { 1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 30000 };

static off_t ngx_http_monitoring_size_bounds[
    NGX_HTTP_MONITORING_SIZE_BUCKETS - 1
] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536,
      262144, 1048576, 10485760 };

static const char *ngx_http_monitoring_methods[NGX_HTTP_MONITORING_METHODS] = {
    "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH", "OTHER"
};


ngx_int_t
ngx_http_monitoring_send_json(ngx_http_request_t *r,
    ngx_http_monitoring_endpoint_e endpoint)
{
    ngx_buf_t                         *b;
    ngx_http_monitoring_json_writer_t  jw;
    ngx_http_monitoring_shctx_t       *sh;
    ngx_str_t                          type = ngx_string("application/json");
    const char                         *scope;

    sh = ngx_http_monitoring_get_shctx(r);
    if (sh == NULL) {
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    b = ngx_create_temp_buf(r->pool, 512 * 1024);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    jw.pos = b->pos;
    jw.last = b->end;
    jw.failed = 0;

    switch (endpoint) {
    case NGX_HTTP_MONITORING_EP_API_SYSTEM:
        scope = "system";
        break;
    case NGX_HTTP_MONITORING_EP_API_NGINX:
        scope = "nginx";
        break;
    case NGX_HTTP_MONITORING_EP_API_NETWORK:
        scope = "network";
        break;
    case NGX_HTTP_MONITORING_EP_API_DISK:
        scope = "disk";
        break;
    case NGX_HTTP_MONITORING_EP_API_PROCESSES:
        scope = "processes";
        break;
    case NGX_HTTP_MONITORING_EP_API_UPSTREAMS:
        scope = "upstreams";
        break;
    case NGX_HTTP_MONITORING_EP_API_CONNECTIONS:
        scope = "connections";
        break;
    case NGX_HTTP_MONITORING_EP_API_REQUESTS:
        scope = "requests";
        break;
    default:
        scope = "full";
        break;
    }

    ngx_http_monitoring_json_header(&jw, r, scope);

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_SYSTEM)
    {
        ngx_http_monitoring_json_append(&jw, ",\"system\":");
        ngx_http_monitoring_json_system(&jw, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_NGINX)
    {
        ngx_http_monitoring_json_append(&jw, ",\"nginx\":");
        ngx_http_monitoring_json_nginx(&jw, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_NETWORK)
    {
        ngx_http_monitoring_json_append(&jw, ",\"network\":");
        ngx_http_monitoring_json_network(&jw, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_DISK)
    {
        ngx_http_monitoring_json_append(&jw, ",\"disk\":");
        ngx_http_monitoring_json_disk(&jw, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_PROCESSES)
    {
        ngx_http_monitoring_json_append(&jw, ",\"processes\":");
        ngx_http_monitoring_json_processes(&jw, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_UPSTREAMS)
    {
        ngx_http_monitoring_json_append(&jw, ",\"upstreams\":");
        ngx_http_monitoring_json_upstreams(&jw, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_CONNECTIONS)
    {
        ngx_http_monitoring_json_append(&jw, ",\"connections\":");
        ngx_http_monitoring_json_connections(&jw, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL
        || endpoint == NGX_HTTP_MONITORING_EP_API_REQUESTS)
    {
        ngx_http_monitoring_json_append(&jw, ",\"requests\":");
        ngx_http_monitoring_json_requests(&jw, r, sh);
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_API_FULL) {
        ngx_http_monitoring_json_append(&jw, ",\"history\":");
        ngx_http_monitoring_json_history(&jw, sh);
    }

    ngx_http_monitoring_json_append(&jw, "}\n");

    if (jw.failed) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->last = jw.pos;
    b->last_buf = 1;

    return ngx_http_monitoring_send_buffer(r, b, &type);
}


ngx_int_t
ngx_http_monitoring_send_prometheus(ngx_http_request_t *r)
{
    ngx_buf_t                         *b;
    ngx_http_monitoring_json_writer_t  jw;
    ngx_http_monitoring_shctx_t       *sh;
    ngx_str_t                          type = ngx_string("text/plain");

    sh = ngx_http_monitoring_get_shctx(r);
    if (sh == NULL) {
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    b = ngx_create_temp_buf(r->pool, 128 * 1024);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    jw.pos = b->pos;
    jw.last = b->end;
    jw.failed = 0;

    ngx_http_monitoring_json_append(&jw,
        "# HELP nginx_monitor_requests_total Total HTTP requests observed by "
        "ngx_http_monitoring_module\n"
        "# TYPE nginx_monitor_requests_total counter\n");
    ngx_http_monitoring_json_printf(&jw, "nginx_monitor_requests_total %uA\n",
                                    sh->requests.total);

    ngx_http_monitoring_json_append(&jw,
        "# HELP nginx_monitor_active_connections Active nginx connections\n"
        "# TYPE nginx_monitor_active_connections gauge\n");
    ngx_http_monitoring_json_printf(&jw,
        "nginx_monitor_active_connections %uA\n", sh->connections.active);

    ngx_http_monitoring_json_append(&jw,
        "# HELP nginx_monitor_cpu_usage_ratio CPU usage ratio\n"
        "# TYPE nginx_monitor_cpu_usage_ratio gauge\n");
    ngx_http_monitoring_json_printf(&jw,
        "nginx_monitor_cpu_usage_ratio %.3f\n",
        (double) sh->system.usage_milli / 100000.0);

    ngx_http_monitoring_json_append(&jw,
        "# HELP nginx_monitor_memory_used_ratio Memory usage ratio\n"
        "# TYPE nginx_monitor_memory_used_ratio gauge\n");
    if (sh->system.mem_total > 0) {
        ngx_http_monitoring_json_printf(&jw,
            "nginx_monitor_memory_used_ratio %.3f\n",
            (double) (sh->system.mem_total - sh->system.mem_available)
            / (double) sh->system.mem_total);
    } else {
        ngx_http_monitoring_json_append(&jw,
            "nginx_monitor_memory_used_ratio 0\n");
    }

    ngx_http_monitoring_json_append(&jw,
        "# HELP nginx_monitor_latency_p95_ms Estimated p95 request latency\n"
        "# TYPE nginx_monitor_latency_p95_ms gauge\n");
    ngx_http_monitoring_json_printf(&jw,
        "nginx_monitor_latency_p95_ms %.3f\n",
        ngx_http_monitoring_percentile(sh, 95.0));

    if (jw.failed) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->last = jw.pos;
    b->last_buf = 1;

    return ngx_http_monitoring_send_buffer(r, b, &type);
}


ngx_int_t
ngx_http_monitoring_send_health(ngx_http_request_t *r)
{
    ngx_buf_t                         *b;
    ngx_http_monitoring_json_writer_t  jw;
    ngx_http_monitoring_shctx_t       *sh;
    ngx_str_t                          type = ngx_string("application/json");

    sh = ngx_http_monitoring_get_shctx(r);
    if (sh == NULL) {
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    b = ngx_create_temp_buf(r->pool, 4096);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    jw.pos = b->pos;
    jw.last = b->end;
    jw.failed = 0;

    ngx_http_monitoring_json_header(&jw, r, "health");
    ngx_http_monitoring_json_printf(&jw,
        ",\"status\":\"ok\",\"generation\":%uA,\"sse_clients\":%uA}\n",
        sh->generation, sh->connections.sse_clients);

    if (jw.failed) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->last = jw.pos;
    b->last_buf = 1;

    return ngx_http_monitoring_send_buffer(r, b, &type);
}


static void
ngx_http_monitoring_json_append(ngx_http_monitoring_json_writer_t *jw,
    const char *s)
{
    size_t len;

    if (jw->failed) {
        return;
    }

    len = ngx_strlen(s);
    if ((size_t) (jw->last - jw->pos) < len) {
        jw->failed = 1;
        return;
    }

    jw->pos = ngx_cpymem(jw->pos, s, len);
}


static void
ngx_http_monitoring_json_printf(ngx_http_monitoring_json_writer_t *jw,
    const char *fmt, ...)
{
    va_list  args;
    u_char  *p;

    if (jw->failed) {
        return;
    }

    if (jw->last - jw->pos <= 1) {
        jw->failed = 1;
        return;
    }

    va_start(args, fmt);
    p = ngx_vslprintf(jw->pos, jw->last, fmt, args);
    va_end(args);

    if (p >= jw->last) {
        jw->failed = 1;
        return;
    }

    jw->pos = p;
}


static void
ngx_http_monitoring_json_string(ngx_http_monitoring_json_writer_t *jw,
    const u_char *data, size_t len)
{
    size_t i;
    u_char c;

    ngx_http_monitoring_json_append(jw, "\"");

    for (i = 0; i < len && !jw->failed; i++) {
        c = data[i];

        if (c == '"' || c == '\\') {
            if (jw->last - jw->pos < 2) {
                jw->failed = 1;
                return;
            }
            *jw->pos++ = '\\';
            *jw->pos++ = c;
        } else if (c == '\n') {
            ngx_http_monitoring_json_append(jw, "\\n");
        } else if (c == '\r') {
            ngx_http_monitoring_json_append(jw, "\\r");
        } else if (c == '\t') {
            ngx_http_monitoring_json_append(jw, "\\t");
        } else if (c < 0x20) {
            ngx_http_monitoring_json_printf(jw, "\\u%04x", c);
        } else {
            if (jw->last - jw->pos < 1) {
                jw->failed = 1;
                return;
            }
            *jw->pos++ = c;
        }
    }

    ngx_http_monitoring_json_append(jw, "\"");
}


static void
ngx_http_monitoring_json_header(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_request_t *r, const char *scope)
{
    ngx_time_t *tp;

    tp = ngx_timeofday();

    ngx_http_monitoring_json_printf(jw,
        "{\"version\":%d,\"module\":\"%s\",\"timestamp\":%T,"
        "\"msec\":%M,\"scope\":\"%s\",\"pid\":%P",
        NGX_HTTP_MONITORING_VERSION,
        NGX_HTTP_MONITORING_MODULE_VERSION,
        tp->sec, tp->msec, scope, ngx_pid);

    (void) r;
}


static void
ngx_http_monitoring_json_system(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    double mem_used, mem_pct, swap_pct;

    mem_used = 0;
    mem_pct = 0;
    if (sh->system.mem_total > 0) {
        mem_used = (double) (sh->system.mem_total - sh->system.mem_available);
        mem_pct = mem_used * 100.0 / (double) sh->system.mem_total;
    }

    swap_pct = 0;
    if (sh->system.swap_total > 0) {
        swap_pct = (double) (sh->system.swap_total - sh->system.swap_free)
                   * 100.0 / (double) sh->system.swap_total;
    }

    ngx_http_monitoring_json_printf(jw,
        "{\"cpu\":{\"usage\":%.3f,\"cores\":%uA,\"load\":[%.3f,%.3f,%.3f]},"
        "\"memory\":{\"total\":%uA,\"available\":%uA,\"free\":%uA,"
        "\"used\":%.0f,\"used_pct\":%.3f},"
        "\"swap\":{\"total\":%uA,\"free\":%uA,\"used_pct\":%.3f},"
        "\"uptime\":%uA}",
        (double) sh->system.usage_milli / 1000.0,
        sh->system.cores,
        (double) sh->system.load1_milli / 1000.0,
        (double) sh->system.load5_milli / 1000.0,
        (double) sh->system.load15_milli / 1000.0,
        sh->system.mem_total, sh->system.mem_available, sh->system.mem_free,
        mem_used, mem_pct,
        sh->system.swap_total, sh->system.swap_free, swap_pct,
        sh->system.uptime);
}


static void
ngx_http_monitoring_json_network(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_uint_t          i, count;
    ngx_atomic_uint_t   rx, tx, rx_packets, tx_packets, rx_errors, tx_errors;

    rx = tx = rx_packets = tx_packets = rx_errors = tx_errors = 0;
    count = ngx_min((ngx_uint_t) sh->iface_count,
                    NGX_HTTP_MONITORING_IFACES_MAX);

    for (i = 0; i < count; i++) {
        rx += sh->ifaces[i].rx_bytes;
        tx += sh->ifaces[i].tx_bytes;
        rx_packets += sh->ifaces[i].rx_packets;
        tx_packets += sh->ifaces[i].tx_packets;
        rx_errors += sh->ifaces[i].rx_errors;
        tx_errors += sh->ifaces[i].tx_errors;
    }

    ngx_http_monitoring_json_printf(jw,
        "{\"rx_bytes\":%uA,\"tx_bytes\":%uA,\"rx_packets\":%uA,"
        "\"tx_packets\":%uA,\"rx_errors\":%uA,\"tx_errors\":%uA,"
        "\"interfaces\":[",
        rx, tx, rx_packets, tx_packets, rx_errors, tx_errors);

    for (i = 0; i < count; i++) {
        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }

        ngx_http_monitoring_json_append(jw, "{\"name\":");
        ngx_http_monitoring_json_string(jw, sh->ifaces[i].name,
                                        ngx_strlen(sh->ifaces[i].name));
        ngx_http_monitoring_json_printf(jw,
            ",\"rx_bytes\":%uA,\"tx_bytes\":%uA,\"rx_packets\":%uA,"
            "\"tx_packets\":%uA,\"rx_errors\":%uA,\"tx_errors\":%uA,"
            "\"up\":%s}",
            sh->ifaces[i].rx_bytes, sh->ifaces[i].tx_bytes,
            sh->ifaces[i].rx_packets, sh->ifaces[i].tx_packets,
            sh->ifaces[i].rx_errors, sh->ifaces[i].tx_errors,
            (sh->ifaces[i].flags & IFF_UP) ? "true" : "false");
    }

    ngx_http_monitoring_json_append(jw, "]}");
}


static void
ngx_http_monitoring_json_disk(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_uint_t          i, count;
    ngx_atomic_uint_t   reads, writes, read_bytes, write_bytes;

    reads = writes = read_bytes = write_bytes = 0;
    count = ngx_min((ngx_uint_t) sh->disk_count,
                    NGX_HTTP_MONITORING_DISKS_MAX);

    for (i = 0; i < count; i++) {
        reads += sh->disks[i].reads;
        writes += sh->disks[i].writes;
        read_bytes += sh->disks[i].read_bytes;
        write_bytes += sh->disks[i].write_bytes;
    }

    ngx_http_monitoring_json_printf(jw,
        "{\"reads\":%uA,\"writes\":%uA,\"read_bytes\":%uA,"
        "\"write_bytes\":%uA,\"devices\":[",
        reads, writes, read_bytes, write_bytes);

    for (i = 0; i < count; i++) {
        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }

        ngx_http_monitoring_json_append(jw, "{\"name\":");
        ngx_http_monitoring_json_string(jw, sh->disks[i].name,
                                        ngx_strlen(sh->disks[i].name));
        ngx_http_monitoring_json_printf(jw,
            ",\"reads\":%uA,\"writes\":%uA,\"read_bytes\":%uA,"
            "\"write_bytes\":%uA,\"io_ms\":%uA}",
            sh->disks[i].reads, sh->disks[i].writes,
            sh->disks[i].read_bytes, sh->disks[i].write_bytes,
            sh->disks[i].io_ms);
    }

    ngx_http_monitoring_json_append(jw, "],\"filesystems\":[");

    count = ngx_min((ngx_uint_t) sh->fs_count,
                    NGX_HTTP_MONITORING_FILESYSTEMS_MAX);

    for (i = 0; i < count; i++) {
        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }

        ngx_http_monitoring_json_append(jw, "{\"path\":");
        ngx_http_monitoring_json_string(jw, sh->filesystems[i].path,
            ngx_strlen(sh->filesystems[i].path));
        ngx_http_monitoring_json_append(jw, ",\"type\":");
        ngx_http_monitoring_json_string(jw, sh->filesystems[i].type,
            ngx_strlen(sh->filesystems[i].type));
        ngx_http_monitoring_json_printf(jw,
            ",\"total\":%uA,\"used\":%uA,\"free\":%uA,\"avail\":%uA,"
            "\"files\":%uA,\"files_free\":%uA}",
            sh->filesystems[i].total, sh->filesystems[i].used,
            sh->filesystems[i].free, sh->filesystems[i].avail,
            sh->filesystems[i].files, sh->filesystems[i].files_free);
    }

    ngx_http_monitoring_json_append(jw, "]}");
}


static void
ngx_http_monitoring_json_processes(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_http_monitoring_json_printf(jw,
        "{\"process_count\":%uA,\"tcp\":{\"established\":%uA,"
        "\"listen\":%uA},\"sockets\":{\"used\":%uA,\"tcp\":%uA,"
        "\"udp\":%uA},\"workers\":",
        sh->system.process_count, sh->system.tcp_established,
        sh->system.tcp_listen, sh->system.sockets_used,
        sh->system.sockets_tcp, sh->system.sockets_udp);

    ngx_http_monitoring_json_workers(jw, sh);
    ngx_http_monitoring_json_append(jw, "}");
}


static void
ngx_http_monitoring_json_nginx(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_http_monitoring_json_printf(jw,
        "{\"connections\":{\"accepted\":%uA,\"handled\":%uA,\"active\":%uA,"
        "\"reading\":%uA,\"writing\":%uA,\"waiting\":%uA},"
        "\"requests\":{\"total\":%uA,\"responses\":%uA,"
        "\"requests_per_sec\":%.3f,\"responses_per_sec\":%.3f},"
        "\"ssl\":{\"requests\":%uA,\"handshakes\":%uA},"
        "\"keepalive\":{\"requests\":%uA},\"workers\":",
        sh->connections.accepted, sh->connections.handled,
        sh->connections.active, sh->connections.reading,
        sh->connections.writing, sh->connections.waiting,
        sh->requests.total, sh->requests.responses,
        (double) sh->ewma_rps_milli / 1000.0,
        (double) sh->ewma_resp_milli / 1000.0,
        sh->connections.ssl_requests, sh->connections.ssl_handshakes,
        sh->connections.keepalive_requests);

    ngx_http_monitoring_json_workers(jw, sh);
    ngx_http_monitoring_json_append(jw, "}");
}


static void
ngx_http_monitoring_json_connections(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_http_monitoring_json_printf(jw,
        "{\"accepted\":%uA,\"handled\":%uA,\"active\":%uA,"
        "\"reading\":%uA,\"writing\":%uA,\"waiting\":%uA,"
        "\"ssl_requests\":%uA,\"ssl_handshakes\":%uA,"
        "\"keepalive_requests\":%uA,\"sse_clients\":%uA,"
        "\"sse_events\":%uA,\"rate_limited\":%uA}",
        sh->connections.accepted, sh->connections.handled,
        sh->connections.active, sh->connections.reading,
        sh->connections.writing, sh->connections.waiting,
        sh->connections.ssl_requests, sh->connections.ssl_handshakes,
        sh->connections.keepalive_requests, sh->connections.sse_clients,
        sh->connections.sse_events, sh->connections.rate_limited);
}


static void
ngx_http_monitoring_json_requests(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_request_t *r, ngx_http_monitoring_shctx_t *sh)
{
    ngx_uint_t i;
    double     avg_latency;

    avg_latency = 0;
    if (sh->requests.total > 0) {
        avg_latency = (double) sh->requests.request_time_ms_total
                      / (double) sh->requests.total;
    }

    ngx_http_monitoring_json_printf(jw,
        "{\"total\":%uA,\"responses\":%uA,\"bytes\":%uA,"
        "\"requests_per_sec\":%.3f,\"responses_per_sec\":%.3f,"
        "\"error_rate\":%.6f,"
        "\"latency\":{\"avg\":%.3f,\"p50\":%.3f,\"p90\":%.3f,"
        "\"p95\":%.3f,\"p99\":%.3f},"
        "\"status\":{\"1xx\":%uA,\"2xx\":%uA,\"3xx\":%uA,"
        "\"4xx\":%uA,\"5xx\":%uA},\"methods\":{",
        sh->requests.total, sh->requests.responses, sh->requests.bytes,
        (double) sh->ewma_rps_milli / 1000.0,
        (double) sh->ewma_resp_milli / 1000.0,
        sh->requests.total > 0
            ? (double) (sh->requests.status_4xx + sh->requests.status_5xx)
              / (double) sh->requests.total
            : 0.0,
        avg_latency,
        ngx_http_monitoring_percentile(sh, 50.0),
        ngx_http_monitoring_percentile(sh, 90.0),
        ngx_http_monitoring_percentile(sh, 95.0),
        ngx_http_monitoring_percentile(sh, 99.0),
        sh->requests.status_1xx, sh->requests.status_2xx,
        sh->requests.status_3xx, sh->requests.status_4xx,
        sh->requests.status_5xx);

    for (i = 0; i < NGX_HTTP_MONITORING_METHODS; i++) {
        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }
        ngx_http_monitoring_json_printf(jw, "\"%s\":%uA",
                                        ngx_http_monitoring_methods[i],
                                        sh->requests.method[i]);
    }

    ngx_http_monitoring_json_append(jw, "},\"latency_histogram\":[");

    for (i = 0; i < NGX_HTTP_MONITORING_LATENCY_BUCKETS; i++) {
        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }
        if (i < NGX_HTTP_MONITORING_LATENCY_BUCKETS - 1) {
            ngx_http_monitoring_json_printf(jw,
                "{\"le\":%ui,\"count\":%uA}",
                ngx_http_monitoring_latency_bounds[i],
                sh->requests.latency_bucket[i]);
        } else {
            ngx_http_monitoring_json_printf(jw,
                "{\"le\":\"+Inf\",\"count\":%uA}",
                sh->requests.latency_bucket[i]);
        }
    }

    ngx_http_monitoring_json_append(jw, "],\"size_histogram\":[");

    for (i = 0; i < NGX_HTTP_MONITORING_SIZE_BUCKETS; i++) {
        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }
        if (i < NGX_HTTP_MONITORING_SIZE_BUCKETS - 1) {
            ngx_http_monitoring_json_printf(jw,
                "{\"le\":%O,\"count\":%uA}",
                ngx_http_monitoring_size_bounds[i],
                sh->requests.size_bucket[i]);
        } else {
            ngx_http_monitoring_json_printf(jw,
                "{\"le\":\"+Inf\",\"count\":%uA}",
                sh->requests.size_bucket[i]);
        }
    }

    ngx_http_monitoring_json_append(jw, "],\"top_urls\":");
    ngx_http_monitoring_json_top_entries(jw, r, sh->urls,
        NGX_HTTP_MONITORING_TOP_URLS_MAX, "url");

    ngx_http_monitoring_json_append(jw, ",\"user_agents\":");
    ngx_http_monitoring_json_top_entries(jw, r, sh->user_agents,
        NGX_HTTP_MONITORING_TOP_UA_MAX, "user_agent");

    ngx_http_monitoring_json_append(jw, "}");
}


static void
ngx_http_monitoring_json_upstreams(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_uint_t                           i, count, emitted;
    ngx_http_monitoring_upstream_entry_t *u;
    double                               avg;

    count = NGX_HTTP_MONITORING_UPSTREAMS_MAX;
    emitted = 0;

    ngx_http_monitoring_json_append(jw, "[");

    for (i = 0; i < count; i++) {
        u = &sh->upstreams[i];
        if (u->requests == 0) {
            continue;
        }

        if (emitted++) {
            ngx_http_monitoring_json_append(jw, ",");
        }

        avg = u->requests > 0
              ? (double) u->latency_ms_total / (double) u->requests
              : 0.0;

        ngx_http_monitoring_json_append(jw, "{\"peer\":");
        ngx_http_monitoring_json_string(jw, u->peer, ngx_strlen(u->peer));
        ngx_http_monitoring_json_printf(jw,
            ",\"requests\":%uA,\"failures\":%uA,\"status_4xx\":%uA,"
            "\"status_5xx\":%uA,\"avg_latency\":%.3f,\"last_seen\":%uA}",
            u->requests, u->failures, u->status_4xx, u->status_5xx,
            avg, u->last_seen);
    }

    ngx_http_monitoring_json_append(jw, "]");
}


static void
ngx_http_monitoring_json_history(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_uint_t                           i, idx, count, capacity, write;
    ngx_http_monitoring_history_sample_t *s;

    count = ngx_min((ngx_uint_t) sh->history_count,
                    NGX_HTTP_MONITORING_HISTORY_MAX);
    capacity = ngx_min((ngx_uint_t) sh->history_capacity,
                       NGX_HTTP_MONITORING_HISTORY_MAX);

    if (capacity == 0) {
        ngx_http_monitoring_json_append(jw, "[]");
        return;
    }

    write = (ngx_uint_t) sh->history_write;

    ngx_http_monitoring_json_append(jw, "[");

    for (i = 0; i < count; i++) {
        idx = (write + capacity - count + i) % capacity;
        s = &sh->history[idx];

        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }

        ngx_http_monitoring_json_printf(jw,
            "{\"timestamp\":%T,\"cpu\":%.3f,\"memory\":%.3f,"
            "\"swap\":%.3f,\"rps\":%.3f,\"responses_per_sec\":%.3f,"
            "\"network_rx_per_sec\":%.3f,\"network_tx_per_sec\":%.3f,"
            "\"disk_read_per_sec\":%.3f,\"disk_write_per_sec\":%.3f,"
            "\"latency_p95\":%.3f,\"requests_total\":%uA,"
            "\"status_4xx\":%uA,\"status_5xx\":%uA}",
            s->timestamp, s->cpu_usage, s->memory_used_pct,
            s->swap_used_pct, s->requests_per_sec, s->responses_per_sec,
            s->network_rx_per_sec, s->network_tx_per_sec,
            s->disk_read_per_sec, s->disk_write_per_sec,
            s->latency_p95, s->requests_total, s->status_4xx,
            s->status_5xx);
    }

    ngx_http_monitoring_json_append(jw, "]");
}


static void
ngx_http_monitoring_json_workers(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_uint_t i, emitted;

    emitted = 0;
    ngx_http_monitoring_json_append(jw, "[");

    for (i = 0; i < NGX_HTTP_MONITORING_WORKERS_MAX; i++) {
        if (sh->workers[i].pid == 0) {
            continue;
        }

        if (emitted++) {
            ngx_http_monitoring_json_append(jw, ",");
        }

        ngx_http_monitoring_json_printf(jw,
            "{\"slot\":%ui,\"pid\":%uA,\"active\":%s,\"requests\":%uA,"
            "\"bytes\":%uA,\"errors\":%uA,\"vm_size\":%uA,"
            "\"vm_rss\":%uA,\"voluntary_ctxt\":%uA,"
            "\"nonvoluntary_ctxt\":%uA,\"last_seen\":%uA}",
            i, sh->workers[i].pid,
            sh->workers[i].active ? "true" : "false",
            sh->workers[i].requests, sh->workers[i].bytes,
            sh->workers[i].errors, sh->workers[i].vm_size,
            sh->workers[i].vm_rss, sh->workers[i].voluntary_ctxt,
            sh->workers[i].nonvoluntary_ctxt, sh->workers[i].last_seen);
    }

    ngx_http_monitoring_json_append(jw, "]");
}


static void
ngx_http_monitoring_json_top_entries(ngx_http_monitoring_json_writer_t *jw,
    ngx_http_request_t *r, ngx_http_monitoring_top_entry_t *entries,
    ngx_uint_t capacity, const char *key_name)
{
    ngx_uint_t                       i, n;
    ngx_http_monitoring_top_view_t  *view;
    double                           avg;

    view = ngx_pcalloc(r->pool, capacity * sizeof(ngx_http_monitoring_top_view_t));
    if (view == NULL) {
        jw->failed = 1;
        return;
    }

    n = 0;
    for (i = 0; i < capacity; i++) {
        if (entries[i].hits == 0) {
            continue;
        }

        ngx_cpystrn(view[n].key, entries[i].key, NGX_HTTP_MONITORING_KEY_LEN);
        view[n].hits = entries[i].hits;
        view[n].errors = entries[i].errors;
        view[n].bytes = entries[i].bytes;
        view[n].latency_ms_total = entries[i].latency_ms_total;
        view[n].last_seen = entries[i].last_seen;
        n++;
    }

    ngx_http_monitoring_sort_top(view, n);

    ngx_http_monitoring_json_append(jw, "[");
    for (i = 0; i < n; i++) {
        if (i) {
            ngx_http_monitoring_json_append(jw, ",");
        }

        avg = view[i].hits > 0
              ? (double) view[i].latency_ms_total / (double) view[i].hits
              : 0.0;

        ngx_http_monitoring_json_append(jw, "{\"");
        ngx_http_monitoring_json_append(jw, key_name);
        ngx_http_monitoring_json_append(jw, "\":");
        ngx_http_monitoring_json_string(jw, view[i].key,
                                        ngx_strlen(view[i].key));
        ngx_http_monitoring_json_printf(jw,
            ",\"hits\":%uA,\"errors\":%uA,\"bytes\":%uA,"
            "\"avg_latency\":%.3f,\"last_seen\":%uA}",
            view[i].hits, view[i].errors, view[i].bytes, avg,
            view[i].last_seen);
    }
    ngx_http_monitoring_json_append(jw, "]");
}


static ngx_int_t
ngx_http_monitoring_send_buffer(ngx_http_request_t *r, ngx_buf_t *b,
    ngx_str_t *type)
{
    ngx_chain_t out;

    ngx_http_discard_request_body(r);
    ngx_http_monitoring_add_no_store(r);

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;
    r->headers_out.content_type = *type;

    if (r->method == NGX_HTTP_HEAD) {
        r->header_only = 1;
        return ngx_http_send_header(r);
    }

    out.buf = b;
    out.next = NULL;

    ngx_http_send_header(r);
    return ngx_http_output_filter(r, &out);
}


static void
ngx_http_monitoring_add_no_store(ngx_http_request_t *r)
{
    ngx_table_elt_t *h;

    h = ngx_list_push(&r->headers_out.headers);
    if (h != NULL) {
        h->hash = 1;
        ngx_str_set(&h->key, "Cache-Control");
        ngx_str_set(&h->value, "no-store");
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h != NULL) {
        h->hash = 1;
        ngx_str_set(&h->key, "X-Content-Type-Options");
        ngx_str_set(&h->value, "nosniff");
    }
}


static void
ngx_http_monitoring_sort_top(ngx_http_monitoring_top_view_t *v, ngx_uint_t n)
{
    ngx_uint_t                      i, j;
    ngx_http_monitoring_top_view_t  tmp;

    for (i = 1; i < n; i++) {
        tmp = v[i];
        j = i;

        while (j > 0 && v[j - 1].hits < tmp.hits) {
            v[j] = v[j - 1];
            j--;
        }

        v[j] = tmp;
    }
}

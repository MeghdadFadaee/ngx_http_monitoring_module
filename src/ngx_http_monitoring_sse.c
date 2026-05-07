#include "ngx_http_monitoring.h"

typedef struct {
    ngx_http_request_t  *r;
    ngx_event_t          event;
    ngx_msec_t           interval;
    ngx_uint_t           closed;
    ngx_uint_t           sequence;
    ngx_uint_t           slot;
    u_char               data[2][16384];
    ngx_buf_t            bufs[2];
    ngx_chain_t          chains[2];
} ngx_http_monitoring_sse_client_t;

static void ngx_http_monitoring_sse_timer(ngx_event_t *ev);
static void ngx_http_monitoring_sse_cleanup(void *data);
static ngx_int_t ngx_http_monitoring_sse_write(
    ngx_http_monitoring_sse_client_t *client);
static u_char *ngx_http_monitoring_sse_payload(u_char *p, u_char *last,
    ngx_http_monitoring_sse_client_t *client,
    ngx_http_monitoring_shctx_t *sh);
static ngx_atomic_uint_t ngx_http_monitoring_sse_sum_net(
    ngx_http_monitoring_shctx_t *sh, ngx_uint_t tx);
static ngx_atomic_uint_t ngx_http_monitoring_sse_sum_disk(
    ngx_http_monitoring_shctx_t *sh, ngx_uint_t write);
static void ngx_http_monitoring_sse_add_header(ngx_http_request_t *r,
    const char *key, const char *value);


ngx_int_t
ngx_http_monitoring_send_sse(ngx_http_request_t *r)
{
    ngx_int_t                         rc;
    ngx_pool_cleanup_t               *cln;
    ngx_http_monitoring_sse_client_t *client;
    ngx_http_monitoring_main_conf_t  *mmcf;
    ngx_http_monitoring_shctx_t      *sh;

    mmcf = ngx_http_monitoring_get_main_conf(r);
    sh = ngx_http_monitoring_get_shctx(r);
    if (mmcf == NULL || sh == NULL) {
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }

    ngx_http_discard_request_body(r);

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = -1;
    ngx_str_set(&r->headers_out.content_type, "text/event-stream");
    ngx_http_monitoring_sse_add_header(r, "Cache-Control", "no-store");
    ngx_http_monitoring_sse_add_header(r, "X-Accel-Buffering", "no");
    ngx_http_monitoring_sse_add_header(r, "Connection", "keep-alive");

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    client = ngx_pcalloc(r->pool, sizeof(ngx_http_monitoring_sse_client_t));
    if (client == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    client->r = r;
    client->interval = ngx_max(mmcf->refresh_interval, 1000);
    client->event.handler = ngx_http_monitoring_sse_timer;
    client->event.data = client;
    client->event.log = r->connection->log;

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cln->handler = ngx_http_monitoring_sse_cleanup;
    cln->data = client;

    ngx_atomic_fetch_add(&sh->connections.sse_clients, 1);

    r->main->count++;

    if (ngx_http_monitoring_sse_write(client) == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return NGX_DONE;
    }

    ngx_add_timer(&client->event, client->interval);

    return NGX_DONE;
}


static void
ngx_http_monitoring_sse_timer(ngx_event_t *ev)
{
    ngx_http_monitoring_sse_client_t *client;
    ngx_http_request_t               *r;

    client = ev->data;
    r = client->r;

    if (client->closed || r->connection->error) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (r->connection->buffered) {
        ngx_add_timer(ev, client->interval);
        return;
    }

    if (ngx_http_monitoring_sse_write(client) == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    ngx_add_timer(ev, client->interval);
}


static void
ngx_http_monitoring_sse_cleanup(void *data)
{
    ngx_http_monitoring_sse_client_t *client = data;
    ngx_http_monitoring_shctx_t      *sh;

    if (client->closed) {
        return;
    }

    client->closed = 1;

    if (client->event.timer_set) {
        ngx_del_timer(&client->event);
    }

    sh = ngx_http_monitoring_get_shctx(client->r);
    if (sh != NULL && sh->connections.sse_clients > 0) {
        ngx_atomic_fetch_add(&sh->connections.sse_clients, -1);
    }
}


static ngx_int_t
ngx_http_monitoring_sse_write(ngx_http_monitoring_sse_client_t *client)
{
    ngx_http_request_t           *r;
    ngx_http_monitoring_shctx_t  *sh;
    ngx_buf_t                    *b;
    ngx_chain_t                  *out;
    u_char                       *p, *last;

    r = client->r;
    sh = ngx_http_monitoring_get_shctx(r);
    if (sh == NULL) {
        return NGX_ERROR;
    }

    client->slot ^= 1;
    p = client->data[client->slot];
    last = p + sizeof(client->data[client->slot]);

    p = ngx_slprintf(p, last, "retry: 3000\n: heartbeat %ui\n",
                     client->sequence);
    p = ngx_http_monitoring_sse_payload(p, last, client, sh);
    if (p == last) {
        return NGX_ERROR;
    }

    b = &client->bufs[client->slot];
    ngx_memzero(b, sizeof(ngx_buf_t));
    b->pos = client->data[client->slot];
    b->last = p;
    b->memory = 1;
    b->flush = 1;

    out = &client->chains[client->slot];
    out->buf = b;
    out->next = NULL;

    if (ngx_http_output_filter(r, out) == NGX_ERROR) {
        return NGX_ERROR;
    }

    ngx_atomic_fetch_add(&sh->connections.sse_events, 1);
    client->sequence++;

    return NGX_OK;
}


static u_char *
ngx_http_monitoring_sse_payload(u_char *p, u_char *last,
    ngx_http_monitoring_sse_client_t *client,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_time_t          *tp;
    ngx_atomic_uint_t    rx, tx, dr, dw;
    double               mem_pct;

    tp = ngx_timeofday();
    rx = ngx_http_monitoring_sse_sum_net(sh, 0);
    tx = ngx_http_monitoring_sse_sum_net(sh, 1);
    dr = ngx_http_monitoring_sse_sum_disk(sh, 0);
    dw = ngx_http_monitoring_sse_sum_disk(sh, 1);

    mem_pct = 0;
    if (sh->system.mem_total > 0) {
        mem_pct = ((double) (sh->system.mem_total - sh->system.mem_available)
                   * 100.0) / (double) sh->system.mem_total;
    }

    return ngx_slprintf(p, last,
        "id: %ui\n"
        "event: metrics\n"
        "data: {\"version\":%d,\"timestamp\":%T,\"sequence\":%ui,"
        "\"system\":{\"cpu\":{\"usage\":%.3f,\"cores\":%uA},"
        "\"memory\":{\"used_pct\":%.3f,\"total\":%uA,"
        "\"available\":%uA}},"
        "\"requests\":{\"total\":%uA,\"requests_per_sec\":%.3f,"
        "\"responses_per_sec\":%.3f,\"latency\":{\"p95\":%.3f,"
        "\"p99\":%.3f},\"status\":{\"4xx\":%uA,\"5xx\":%uA}},"
        "\"connections\":{\"active\":%uA,\"reading\":%uA,"
        "\"writing\":%uA,\"waiting\":%uA,\"sse_clients\":%uA,"
        "\"keepalive_requests\":%uA},"
        "\"network\":{\"rx_bytes\":%uA,\"tx_bytes\":%uA},"
        "\"disk\":{\"read_bytes\":%uA,\"write_bytes\":%uA}}\n\n",
        client->sequence, NGX_HTTP_MONITORING_VERSION, tp->sec,
        client->sequence,
        (double) sh->system.usage_milli / 1000.0, sh->system.cores,
        mem_pct, sh->system.mem_total, sh->system.mem_available,
        sh->requests.total, (double) sh->ewma_rps_milli / 1000.0,
        (double) sh->ewma_resp_milli / 1000.0,
        ngx_http_monitoring_percentile(sh, 95.0),
        ngx_http_monitoring_percentile(sh, 99.0),
        sh->requests.status_4xx, sh->requests.status_5xx,
        sh->connections.active, sh->connections.reading,
        sh->connections.writing, sh->connections.waiting,
        sh->connections.sse_clients, sh->connections.keepalive_requests,
        rx, tx, dr, dw);
}


static ngx_atomic_uint_t
ngx_http_monitoring_sse_sum_net(ngx_http_monitoring_shctx_t *sh, ngx_uint_t tx)
{
    ngx_uint_t          i, count;
    ngx_atomic_uint_t   total;

    total = 0;
    count = ngx_min((ngx_uint_t) sh->iface_count,
                    NGX_HTTP_MONITORING_IFACES_MAX);

    for (i = 0; i < count; i++) {
        total += tx ? sh->ifaces[i].tx_bytes : sh->ifaces[i].rx_bytes;
    }

    return total;
}


static ngx_atomic_uint_t
ngx_http_monitoring_sse_sum_disk(ngx_http_monitoring_shctx_t *sh,
    ngx_uint_t write)
{
    ngx_uint_t          i, count;
    ngx_atomic_uint_t   total;

    total = 0;
    count = ngx_min((ngx_uint_t) sh->disk_count,
                    NGX_HTTP_MONITORING_DISKS_MAX);

    for (i = 0; i < count; i++) {
        total += write ? sh->disks[i].write_bytes : sh->disks[i].read_bytes;
    }

    return total;
}


static void
ngx_http_monitoring_sse_add_header(ngx_http_request_t *r, const char *key,
    const char *value)
{
    ngx_table_elt_t *h;

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return;
    }

    h->hash = 1;
    h->key.len = ngx_strlen(key);
    h->key.data = (u_char *) key;
    h->value.len = ngx_strlen(value);
    h->value.data = (u_char *) value;
}

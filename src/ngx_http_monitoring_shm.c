#include "ngx_http_monitoring.h"

static void ngx_http_monitoring_update_worker(ngx_http_monitoring_shctx_t *sh,
    ngx_msec_t latency, off_t bytes, ngx_uint_t is_error);
static void ngx_http_monitoring_update_status(ngx_http_monitoring_shctx_t *sh,
    ngx_uint_t status);
static void ngx_http_monitoring_update_upstream_slot(ngx_slab_pool_t *shpool,
    ngx_http_monitoring_upstream_entry_t *entries, ngx_uint_t capacity,
    ngx_str_t *peer, ngx_msec_t latency, ngx_uint_t status);
static ngx_uint_t ngx_http_monitoring_worker_slot(void);

static ngx_uint_t ngx_http_monitoring_latency_bounds[
    NGX_HTTP_MONITORING_LATENCY_BUCKETS - 1
] = { 1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 30000 };

static off_t ngx_http_monitoring_size_bounds[
    NGX_HTTP_MONITORING_SIZE_BUCKETS - 1
] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536,
      262144, 1048576, 10485760 };


ngx_int_t
ngx_http_monitoring_init_shm(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_monitoring_main_conf_t  *ommcf = data;
    ngx_http_monitoring_main_conf_t  *mmcf;
    ngx_http_monitoring_shctx_t      *sh;
    ngx_slab_pool_t                  *shpool;
    ngx_uint_t                        capacity;

    mmcf = shm_zone->data;

    if (ommcf) {
        mmcf->sh = ommcf->sh;
        mmcf->shpool = ommcf->shpool;
        sh = (ngx_http_monitoring_shctx_t *) mmcf->sh;
        capacity = mmcf->history / ngx_max(mmcf->resolution, 1);
        if (capacity == 0) {
            capacity = 1;
        }
        if (capacity > NGX_HTTP_MONITORING_HISTORY_MAX) {
            capacity = NGX_HTTP_MONITORING_HISTORY_MAX;
        }
        sh->history_capacity = capacity;
        if ((ngx_uint_t) sh->history_count > capacity) {
            sh->history_count = capacity;
        }
        return NGX_OK;
    }

    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        sh = shpool->data;
        if (sh == NULL) {
            return NGX_ERROR;
        }

        mmcf->sh = sh;
        mmcf->shpool = shpool;
        capacity = mmcf->history / ngx_max(mmcf->resolution, 1);
        if (capacity == 0) {
            capacity = 1;
        }
        if (capacity > NGX_HTTP_MONITORING_HISTORY_MAX) {
            capacity = NGX_HTTP_MONITORING_HISTORY_MAX;
        }
        sh->history_capacity = capacity;
        if ((ngx_uint_t) sh->history_count > capacity) {
            sh->history_count = capacity;
        }
        return NGX_OK;
    }

    sh = ngx_slab_alloc(shpool, sizeof(ngx_http_monitoring_shctx_t));
    if (sh == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(sh, sizeof(ngx_http_monitoring_shctx_t));

    capacity = mmcf->history / ngx_max(mmcf->resolution, 1);
    if (capacity == 0) {
        capacity = 1;
    }
    if (capacity > NGX_HTTP_MONITORING_HISTORY_MAX) {
        capacity = NGX_HTTP_MONITORING_HISTORY_MAX;
    }

    sh->history_capacity = capacity;
    sh->initialized = 1;
    sh->generation = 1;
    shpool->data = sh;

    mmcf->sh = sh;
    mmcf->shpool = shpool;

    return NGX_OK;
}


ngx_http_monitoring_shctx_t *
ngx_http_monitoring_get_shctx(ngx_http_request_t *r)
{
    ngx_http_monitoring_main_conf_t *mmcf;

    mmcf = ngx_http_get_module_main_conf(r, ngx_http_monitoring_module);
    if (mmcf == NULL) {
        return NULL;
    }

    return (ngx_http_monitoring_shctx_t *) mmcf->sh;
}


ngx_http_monitoring_main_conf_t *
ngx_http_monitoring_get_main_conf(ngx_http_request_t *r)
{
    return ngx_http_get_module_main_conf(r, ngx_http_monitoring_module);
}


void
ngx_http_monitoring_account_request(ngx_http_request_t *r)
{
    ngx_time_t                       *tp;
    ngx_msec_int_t                    ms;
    ngx_msec_t                        latency;
    off_t                             bytes;
    ngx_uint_t                        status, method, lb, sb, is_error;
    ngx_http_monitoring_shctx_t      *sh;
    ngx_http_monitoring_main_conf_t  *mmcf;
    ngx_str_t                         uri, ua;

    sh = ngx_http_monitoring_get_shctx(r);
    mmcf = ngx_http_monitoring_get_main_conf(r);
    if (sh == NULL || mmcf == NULL) {
        return;
    }

    tp = ngx_timeofday();
    ms = (ngx_msec_int_t) ((tp->sec - r->start_sec) * 1000
                           + (tp->msec - r->start_msec));
    latency = (ms > 0) ? (ngx_msec_t) ms : 0;

    status = r->headers_out.status;
    if (status == 0) {
        status = 499;
    }

    bytes = r->headers_out.content_length_n;
    if (bytes < 0) {
        bytes = 0;
    }

    ngx_atomic_fetch_add(&sh->requests.total, 1);
    ngx_atomic_fetch_add(&sh->requests.responses, 1);
    ngx_atomic_fetch_add(&sh->requests.bytes, (ngx_atomic_int_t) bytes);
    ngx_atomic_fetch_add(&sh->requests.request_time_ms_total, latency);

    method = ngx_http_monitoring_method_index(r->method);
    ngx_atomic_fetch_add(&sh->requests.method[method], 1);

    lb = ngx_http_monitoring_latency_bucket(latency);
    ngx_atomic_fetch_add(&sh->requests.latency_bucket[lb], 1);

    sb = ngx_http_monitoring_size_bucket(bytes);
    ngx_atomic_fetch_add(&sh->requests.size_bucket[sb], 1);

    ngx_http_monitoring_update_status(sh, status);

#if (NGX_HTTP_SSL)
    if (r->connection->ssl) {
        ngx_atomic_fetch_add(&sh->connections.ssl_requests, 1);
        if (r->connection->requests <= 1) {
            ngx_atomic_fetch_add(&sh->connections.ssl_handshakes, 1);
        }
    }
#endif

    if (r->connection->requests > 1) {
        ngx_atomic_fetch_add(&sh->connections.keepalive_requests, 1);
    }

    is_error = status >= 500;
    ngx_http_monitoring_update_worker(sh, latency, bytes, is_error);

    uri = r->uri;
    ngx_http_monitoring_update_top(mmcf->shpool, sh->urls,
                                   ngx_min(mmcf->max_top_urls,
                                           NGX_HTTP_MONITORING_TOP_URLS_MAX),
                                   &uri, latency, bytes,
                                   status >= 400);

    if (r->headers_in.user_agent) {
        ua = r->headers_in.user_agent->value;
        ngx_http_monitoring_update_top(mmcf->shpool, sh->user_agents,
                                       NGX_HTTP_MONITORING_TOP_UA_MAX,
                                       &ua, latency, bytes,
                                       status >= 400);
    }

    ngx_http_monitoring_account_upstream(r, latency);
}


void
ngx_http_monitoring_account_upstream(ngx_http_request_t *r, ngx_msec_t latency)
{
    ngx_uint_t                         i;
    ngx_http_upstream_state_t         *state;
    ngx_http_monitoring_shctx_t       *sh;
    ngx_http_monitoring_main_conf_t   *mmcf;
    ngx_str_t                          peer;
    ngx_msec_t                         response_time;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        return;
    }

    sh = ngx_http_monitoring_get_shctx(r);
    mmcf = ngx_http_monitoring_get_main_conf(r);
    if (sh == NULL || mmcf == NULL || mmcf->shpool == NULL) {
        return;
    }

    state = r->upstream_states->elts;

    for (i = 0; i < r->upstream_states->nelts; i++) {
        if (state[i].peer && state[i].peer->len) {
            peer = *state[i].peer;
        } else {
            ngx_str_set(&peer, "unknown");
        }

        response_time = state[i].response_time;
        if (response_time == (ngx_msec_t) -1) {
            response_time = latency;
        }

        ngx_http_monitoring_update_upstream_slot(mmcf->shpool, sh->upstreams,
            NGX_HTTP_MONITORING_UPSTREAMS_MAX, &peer, response_time,
            state[i].status);
    }
}


static void
ngx_http_monitoring_update_worker(ngx_http_monitoring_shctx_t *sh,
    ngx_msec_t latency, off_t bytes, ngx_uint_t is_error)
{
    ngx_uint_t                         slot;
    ngx_http_monitoring_worker_metric_t *w;

    slot = ngx_http_monitoring_worker_slot();
    w = &sh->workers[slot];

    w->pid = ngx_pid;
    w->last_seen = ngx_time();
    ngx_atomic_fetch_add(&w->requests, 1);
    ngx_atomic_fetch_add(&w->bytes, (ngx_atomic_int_t) bytes);

    if (latency > 30000) {
        ngx_atomic_fetch_add(&w->errors, 1);
    }

    if (is_error) {
        ngx_atomic_fetch_add(&w->errors, 1);
    }
}


static void
ngx_http_monitoring_update_status(ngx_http_monitoring_shctx_t *sh,
    ngx_uint_t status)
{
    if (status < 200) {
        ngx_atomic_fetch_add(&sh->requests.status_1xx, 1);
    } else if (status < 300) {
        ngx_atomic_fetch_add(&sh->requests.status_2xx, 1);
    } else if (status < 400) {
        ngx_atomic_fetch_add(&sh->requests.status_3xx, 1);
    } else if (status < 500) {
        ngx_atomic_fetch_add(&sh->requests.status_4xx, 1);
    } else {
        ngx_atomic_fetch_add(&sh->requests.status_5xx, 1);
    }
}


static void
ngx_http_monitoring_update_upstream_slot(ngx_slab_pool_t *shpool,
    ngx_http_monitoring_upstream_entry_t *entries, ngx_uint_t capacity,
    ngx_str_t *peer, ngx_msec_t latency, ngx_uint_t status)
{
    ngx_uint_t                         i, victim, empty, failure;
    ngx_atomic_uint_t                  min_hits, hits;
    uint32_t                           hash;
    size_t                             len;
    ngx_http_monitoring_upstream_entry_t *e;

    if (peer->len == 0 || capacity == 0) {
        return;
    }

    hash = ngx_http_monitoring_hash(peer);
    empty = capacity;
    victim = 0;
    min_hits = (ngx_atomic_uint_t) -1;

    for (i = 0; i < capacity; i++) {
        e = &entries[i];

        if (e->requests == 0 && empty == capacity) {
            empty = i;
            continue;
        }

        if (e->hash == hash
            && ngx_strncmp(e->peer, peer->data,
                           ngx_min(peer->len, NGX_HTTP_MONITORING_KEY_LEN - 1))
               == 0
            && e->peer[ngx_min(peer->len, NGX_HTTP_MONITORING_KEY_LEN - 1)]
               == '\0')
        {
            failure = (status == 0 || status >= 500);
            ngx_atomic_fetch_add(&e->requests, 1);
            ngx_atomic_fetch_add(&e->latency_ms_total, latency);
            e->last_seen = ngx_time();
            if (failure) {
                ngx_atomic_fetch_add(&e->failures, 1);
            }
            if (status >= 400 && status < 500) {
                ngx_atomic_fetch_add(&e->status_4xx, 1);
            }
            if (status >= 500) {
                ngx_atomic_fetch_add(&e->status_5xx, 1);
            }
            return;
        }

        hits = e->requests;
        if (hits < min_hits) {
            min_hits = hits;
            victim = i;
        }
    }

    ngx_shmtx_lock(&shpool->mutex);

    i = (empty != capacity) ? empty : victim;
    e = &entries[i];
    ngx_memzero(e, sizeof(ngx_http_monitoring_upstream_entry_t));

    len = ngx_min(peer->len, NGX_HTTP_MONITORING_KEY_LEN - 1);
    ngx_memcpy(e->peer, peer->data, len);
    e->peer[len] = '\0';
    e->hash = hash;
    e->last_seen = ngx_time();

    ngx_shmtx_unlock(&shpool->mutex);

    failure = (status == 0 || status >= 500);
    ngx_atomic_fetch_add(&e->requests, 1);
    ngx_atomic_fetch_add(&e->latency_ms_total, latency);
    if (failure) {
        ngx_atomic_fetch_add(&e->failures, 1);
    }
    if (status >= 400 && status < 500) {
        ngx_atomic_fetch_add(&e->status_4xx, 1);
    }
    if (status >= 500) {
        ngx_atomic_fetch_add(&e->status_5xx, 1);
    }
}


void
ngx_http_monitoring_update_top(ngx_slab_pool_t *shpool,
    ngx_http_monitoring_top_entry_t *entries, ngx_uint_t capacity,
    ngx_str_t *key, ngx_msec_t latency, off_t bytes, ngx_uint_t is_error)
{
    ngx_uint_t                    i, empty, victim;
    ngx_atomic_uint_t             min_hits, hits;
    uint32_t                      hash;
    size_t                        len;
    ngx_http_monitoring_top_entry_t *e;

    if (shpool == NULL || key->len == 0 || capacity == 0) {
        return;
    }

    hash = ngx_http_monitoring_hash(key);
    empty = capacity;
    victim = 0;
    min_hits = (ngx_atomic_uint_t) -1;
    len = ngx_min(key->len, NGX_HTTP_MONITORING_KEY_LEN - 1);

    for (i = 0; i < capacity; i++) {
        e = &entries[i];

        if (e->hits == 0 && empty == capacity) {
            empty = i;
            continue;
        }

        if (e->hash == hash
            && ngx_strncmp(e->key, key->data, len) == 0
            && e->key[len] == '\0')
        {
            ngx_atomic_fetch_add(&e->hits, 1);
            ngx_atomic_fetch_add(&e->bytes, (ngx_atomic_int_t) bytes);
            ngx_atomic_fetch_add(&e->latency_ms_total, latency);
            e->last_seen = ngx_time();
            if (is_error) {
                ngx_atomic_fetch_add(&e->errors, 1);
            }
            return;
        }

        hits = e->hits;
        if (hits < min_hits) {
            min_hits = hits;
            victim = i;
        }
    }

    ngx_shmtx_lock(&shpool->mutex);

    i = (empty != capacity) ? empty : victim;
    e = &entries[i];
    ngx_memzero(e, sizeof(ngx_http_monitoring_top_entry_t));

    ngx_memcpy(e->key, key->data, len);
    e->key[len] = '\0';
    e->hash = hash;
    e->last_seen = ngx_time();

    ngx_shmtx_unlock(&shpool->mutex);

    ngx_atomic_fetch_add(&e->hits, 1);
    ngx_atomic_fetch_add(&e->bytes, (ngx_atomic_int_t) bytes);
    ngx_atomic_fetch_add(&e->latency_ms_total, latency);
    if (is_error) {
        ngx_atomic_fetch_add(&e->errors, 1);
    }
}


ngx_uint_t
ngx_http_monitoring_method_index(ngx_uint_t method)
{
    if (method & NGX_HTTP_GET) {
        return NGX_HTTP_MONITORING_METHOD_GET;
    }
    if (method & NGX_HTTP_POST) {
        return NGX_HTTP_MONITORING_METHOD_POST;
    }
    if (method & NGX_HTTP_PUT) {
        return NGX_HTTP_MONITORING_METHOD_PUT;
    }
    if (method & NGX_HTTP_DELETE) {
        return NGX_HTTP_MONITORING_METHOD_DELETE;
    }
    if (method & NGX_HTTP_HEAD) {
        return NGX_HTTP_MONITORING_METHOD_HEAD;
    }
    if (method & NGX_HTTP_OPTIONS) {
        return NGX_HTTP_MONITORING_METHOD_OPTIONS;
    }
#ifdef NGX_HTTP_PATCH
    if (method & NGX_HTTP_PATCH) {
        return NGX_HTTP_MONITORING_METHOD_PATCH;
    }
#endif
    return NGX_HTTP_MONITORING_METHOD_OTHER;
}


ngx_uint_t
ngx_http_monitoring_latency_bucket(ngx_msec_t ms)
{
    ngx_uint_t i;

    for (i = 0; i < NGX_HTTP_MONITORING_LATENCY_BUCKETS - 1; i++) {
        if (ms <= ngx_http_monitoring_latency_bounds[i]) {
            return i;
        }
    }

    return NGX_HTTP_MONITORING_LATENCY_BUCKETS - 1;
}


ngx_uint_t
ngx_http_monitoring_size_bucket(off_t bytes)
{
    ngx_uint_t i;

    for (i = 0; i < NGX_HTTP_MONITORING_SIZE_BUCKETS - 1; i++) {
        if (bytes <= ngx_http_monitoring_size_bounds[i]) {
            return i;
        }
    }

    return NGX_HTTP_MONITORING_SIZE_BUCKETS - 1;
}


double
ngx_http_monitoring_percentile(ngx_http_monitoring_shctx_t *sh,
    double percentile)
{
    ngx_uint_t          i;
    ngx_atomic_uint_t   total, count, target;

    total = 0;
    for (i = 0; i < NGX_HTTP_MONITORING_LATENCY_BUCKETS; i++) {
        total += sh->requests.latency_bucket[i];
    }

    if (total == 0) {
        return 0;
    }

    target = (ngx_atomic_uint_t) ((double) total * percentile / 100.0);
    if (target == 0) {
        target = 1;
    }

    count = 0;
    for (i = 0; i < NGX_HTTP_MONITORING_LATENCY_BUCKETS - 1; i++) {
        count += sh->requests.latency_bucket[i];
        if (count >= target) {
            return (double) ngx_http_monitoring_latency_bounds[i];
        }
    }

    return 30000.0;
}


uint32_t
ngx_http_monitoring_hash(ngx_str_t *value)
{
    return ngx_crc32_short(value->data, value->len);
}


static ngx_uint_t
ngx_http_monitoring_worker_slot(void)
{
    if (ngx_process_slot < 0) {
        return 0;
    }

    return ((ngx_uint_t) ngx_process_slot) % NGX_HTTP_MONITORING_WORKERS_MAX;
}

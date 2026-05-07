#ifndef _NGX_HTTP_MONITORING_H_INCLUDED_
#define _NGX_HTTP_MONITORING_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <sys/statvfs.h>
#include <stdint.h>

#define NGX_HTTP_MONITORING_VERSION          1
#define NGX_HTTP_MONITORING_MODULE_VERSION   "1.0.0"

#define NGX_HTTP_MONITORING_DEFAULT_SHM_SIZE  (8 * 1024 * 1024)
#define NGX_HTTP_MONITORING_MIN_INTERVAL     100
#define NGX_HTTP_MONITORING_HISTORY_MAX      3600
#define NGX_HTTP_MONITORING_TOP_URLS_MAX     256
#define NGX_HTTP_MONITORING_TOP_UA_MAX       64
#define NGX_HTTP_MONITORING_UPSTREAMS_MAX    128
#define NGX_HTTP_MONITORING_WORKERS_MAX      128
#define NGX_HTTP_MONITORING_IFACES_MAX       32
#define NGX_HTTP_MONITORING_DISKS_MAX        32
#define NGX_HTTP_MONITORING_FILESYSTEMS_MAX  32
#define NGX_HTTP_MONITORING_LATENCY_BUCKETS  14
#define NGX_HTTP_MONITORING_SIZE_BUCKETS     12
#define NGX_HTTP_MONITORING_METHODS          8

#define NGX_HTTP_MONITORING_KEY_LEN          128
#define NGX_HTTP_MONITORING_NAME_LEN         64

typedef enum {
    NGX_HTTP_MONITORING_EP_NONE = 0,
    NGX_HTTP_MONITORING_EP_DASHBOARD,
    NGX_HTTP_MONITORING_EP_API_FULL,
    NGX_HTTP_MONITORING_EP_API_SYSTEM,
    NGX_HTTP_MONITORING_EP_API_NGINX,
    NGX_HTTP_MONITORING_EP_API_NETWORK,
    NGX_HTTP_MONITORING_EP_API_DISK,
    NGX_HTTP_MONITORING_EP_API_PROCESSES,
    NGX_HTTP_MONITORING_EP_API_UPSTREAMS,
    NGX_HTTP_MONITORING_EP_API_CONNECTIONS,
    NGX_HTTP_MONITORING_EP_API_REQUESTS,
    NGX_HTTP_MONITORING_EP_LIVE,
    NGX_HTTP_MONITORING_EP_PROMETHEUS,
    NGX_HTTP_MONITORING_EP_HEALTH
} ngx_http_monitoring_endpoint_e;

typedef enum {
    NGX_HTTP_MONITORING_METHOD_GET = 0,
    NGX_HTTP_MONITORING_METHOD_POST,
    NGX_HTTP_MONITORING_METHOD_PUT,
    NGX_HTTP_MONITORING_METHOD_DELETE,
    NGX_HTTP_MONITORING_METHOD_HEAD,
    NGX_HTTP_MONITORING_METHOD_OPTIONS,
    NGX_HTTP_MONITORING_METHOD_PATCH,
    NGX_HTTP_MONITORING_METHOD_OTHER
} ngx_http_monitoring_method_e;

typedef struct {
    ngx_uint_t             deny;
    ngx_cidr_t             cidr;
} ngx_http_monitoring_access_rule_t;

typedef struct {
    ngx_msec_t             refresh_interval;
    ngx_msec_t             history;
    ngx_msec_t             resolution;
    size_t                 shm_size;
    ngx_uint_t             max_top_urls;
    ngx_flag_t             collect_system;
    ngx_flag_t             collect_nginx;
    ngx_flag_t             collect_network;
    ngx_flag_t             access_log;

    ngx_shm_zone_t        *shm_zone;
    ngx_event_t            collector_event;
    ngx_uint_t             collector_active;
    void                  *sh;
    ngx_slab_pool_t       *shpool;
} ngx_http_monitoring_main_conf_t;

typedef struct {
    ngx_flag_t             enabled;
    ngx_flag_t             dashboard;
    ngx_flag_t             api;
    ngx_flag_t             sse;
    ngx_str_t              api_token;
    ngx_str_t              basic_auth;
    ngx_str_t              cors;
    ngx_uint_t             rate_limit;
    ngx_array_t           *access_rules;
} ngx_http_monitoring_loc_conf_t;

typedef struct {
    time_t                 timestamp;
    ngx_msec_t             msec;
    double                 cpu_usage;
    double                 load1;
    double                 load5;
    double                 load15;
    double                 memory_used_pct;
    double                 swap_used_pct;
    double                 requests_per_sec;
    double                 responses_per_sec;
    double                 network_rx_per_sec;
    double                 network_tx_per_sec;
    double                 disk_read_per_sec;
    double                 disk_write_per_sec;
    double                 latency_p95;
    ngx_atomic_uint_t      requests_total;
    ngx_atomic_uint_t      status_4xx;
    ngx_atomic_uint_t      status_5xx;
} ngx_http_monitoring_history_sample_t;

typedef struct {
    ngx_atomic_t           hits;
    ngx_atomic_t           errors;
    ngx_atomic_t           bytes;
    ngx_atomic_t           latency_ms_total;
    ngx_atomic_t           last_seen;
    uint32_t               hash;
    u_char                 key[NGX_HTTP_MONITORING_KEY_LEN];
} ngx_http_monitoring_top_entry_t;

typedef struct {
    ngx_atomic_t           requests;
    ngx_atomic_t           failures;
    ngx_atomic_t           latency_ms_total;
    ngx_atomic_t           status_4xx;
    ngx_atomic_t           status_5xx;
    ngx_atomic_t           last_seen;
    uint32_t               hash;
    u_char                 peer[NGX_HTTP_MONITORING_KEY_LEN];
} ngx_http_monitoring_upstream_entry_t;

typedef struct {
    ngx_atomic_t           pid;
    ngx_atomic_t           requests;
    ngx_atomic_t           bytes;
    ngx_atomic_t           last_seen;
    ngx_atomic_t           active;
    ngx_atomic_t           errors;
    ngx_atomic_t           vm_size;
    ngx_atomic_t           vm_rss;
    ngx_atomic_t           voluntary_ctxt;
    ngx_atomic_t           nonvoluntary_ctxt;
} ngx_http_monitoring_worker_metric_t;

typedef struct {
    u_char                 name[NGX_HTTP_MONITORING_NAME_LEN];
    ngx_atomic_t           rx_bytes;
    ngx_atomic_t           tx_bytes;
    ngx_atomic_t           rx_packets;
    ngx_atomic_t           tx_packets;
    ngx_atomic_t           rx_errors;
    ngx_atomic_t           tx_errors;
    ngx_atomic_t           flags;
} ngx_http_monitoring_iface_metric_t;

typedef struct {
    u_char                 name[NGX_HTTP_MONITORING_NAME_LEN];
    ngx_atomic_t           reads;
    ngx_atomic_t           writes;
    ngx_atomic_t           read_bytes;
    ngx_atomic_t           write_bytes;
    ngx_atomic_t           io_ms;
} ngx_http_monitoring_disk_metric_t;

typedef struct {
    u_char                 path[NGX_HTTP_MONITORING_KEY_LEN];
    u_char                 type[NGX_HTTP_MONITORING_NAME_LEN];
    ngx_atomic_t           total;
    ngx_atomic_t           used;
    ngx_atomic_t           free;
    ngx_atomic_t           avail;
    ngx_atomic_t           files;
    ngx_atomic_t           files_free;
} ngx_http_monitoring_fs_metric_t;

typedef struct {
    ngx_atomic_t           usage_milli;
    ngx_atomic_t           cores;
    ngx_atomic_t           load1_milli;
    ngx_atomic_t           load5_milli;
    ngx_atomic_t           load15_milli;
    ngx_atomic_t           mem_total;
    ngx_atomic_t           mem_available;
    ngx_atomic_t           mem_free;
    ngx_atomic_t           swap_total;
    ngx_atomic_t           swap_free;
    ngx_atomic_t           uptime;
    ngx_atomic_t           process_count;
    ngx_atomic_t           tcp_established;
    ngx_atomic_t           tcp_listen;
    ngx_atomic_t           sockets_used;
    ngx_atomic_t           sockets_tcp;
    ngx_atomic_t           sockets_udp;
} ngx_http_monitoring_system_metric_t;

typedef struct {
    ngx_atomic_t           total;
    ngx_atomic_t           accepted;
    ngx_atomic_t           handled;
    ngx_atomic_t           active;
    ngx_atomic_t           reading;
    ngx_atomic_t           writing;
    ngx_atomic_t           waiting;
    ngx_atomic_t           ssl_requests;
    ngx_atomic_t           ssl_handshakes;
    ngx_atomic_t           keepalive_requests;
    ngx_atomic_t           sse_clients;
    ngx_atomic_t           sse_events;
    ngx_atomic_t           rate_limited;
} ngx_http_monitoring_connection_metric_t;

typedef struct {
    ngx_atomic_t           total;
    ngx_atomic_t           responses;
    ngx_atomic_t           bytes;
    ngx_atomic_t           request_time_ms_total;
    ngx_atomic_t           status_1xx;
    ngx_atomic_t           status_2xx;
    ngx_atomic_t           status_3xx;
    ngx_atomic_t           status_4xx;
    ngx_atomic_t           status_5xx;
    ngx_atomic_t           method[NGX_HTTP_MONITORING_METHODS];
    ngx_atomic_t           latency_bucket[NGX_HTTP_MONITORING_LATENCY_BUCKETS];
    ngx_atomic_t           size_bucket[NGX_HTTP_MONITORING_SIZE_BUCKETS];
} ngx_http_monitoring_request_metric_t;

typedef struct {
    ngx_atomic_t           collector_lock;
    ngx_atomic_t           rate_limit_window;
    ngx_atomic_t           rate_limit_count;
    ngx_atomic_t           history_write;
    ngx_atomic_t           history_count;
    ngx_atomic_t           history_capacity;
    ngx_atomic_t           generation;
    ngx_atomic_t           initialized;

    ngx_http_monitoring_system_metric_t      system;
    ngx_http_monitoring_connection_metric_t  connections;
    ngx_http_monitoring_request_metric_t     requests;

    ngx_atomic_t           iface_count;
    ngx_http_monitoring_iface_metric_t       ifaces[NGX_HTTP_MONITORING_IFACES_MAX];

    ngx_atomic_t           disk_count;
    ngx_http_monitoring_disk_metric_t        disks[NGX_HTTP_MONITORING_DISKS_MAX];

    ngx_atomic_t           fs_count;
    ngx_http_monitoring_fs_metric_t          filesystems[NGX_HTTP_MONITORING_FILESYSTEMS_MAX];

    ngx_http_monitoring_worker_metric_t      workers[NGX_HTTP_MONITORING_WORKERS_MAX];
    ngx_http_monitoring_top_entry_t          urls[NGX_HTTP_MONITORING_TOP_URLS_MAX];
    ngx_http_monitoring_top_entry_t          user_agents[NGX_HTTP_MONITORING_TOP_UA_MAX];
    ngx_http_monitoring_upstream_entry_t     upstreams[NGX_HTTP_MONITORING_UPSTREAMS_MAX];

    ngx_http_monitoring_history_sample_t     history[NGX_HTTP_MONITORING_HISTORY_MAX];

    ngx_atomic_t           prev_req_total;
    ngx_atomic_t           prev_resp_total;
    ngx_atomic_t           prev_net_rx;
    ngx_atomic_t           prev_net_tx;
    ngx_atomic_t           prev_disk_read;
    ngx_atomic_t           prev_disk_write;
    ngx_atomic_t           prev_collect_msec;
    ngx_atomic_t           prev_history_msec;
    ngx_atomic_t           ewma_rps_milli;
    ngx_atomic_t           ewma_resp_milli;

    uint64_t               prev_cpu_total;
    uint64_t               prev_cpu_idle;
} ngx_http_monitoring_shctx_t;

extern ngx_module_t ngx_http_monitoring_module;

ngx_int_t ngx_http_monitoring_init_shm(ngx_shm_zone_t *shm_zone, void *data);
ngx_int_t ngx_http_monitoring_init_collector(ngx_cycle_t *cycle);
void ngx_http_monitoring_collect(ngx_event_t *ev);
void ngx_http_monitoring_account_request(ngx_http_request_t *r);
void ngx_http_monitoring_account_upstream(ngx_http_request_t *r, ngx_msec_t latency);

ngx_http_monitoring_endpoint_e ngx_http_monitoring_match_endpoint(ngx_http_request_t *r);
ngx_int_t ngx_http_monitoring_send_json(ngx_http_request_t *r,
    ngx_http_monitoring_endpoint_e endpoint);
ngx_int_t ngx_http_monitoring_send_dashboard(ngx_http_request_t *r);
ngx_int_t ngx_http_monitoring_send_sse(ngx_http_request_t *r);
ngx_int_t ngx_http_monitoring_send_prometheus(ngx_http_request_t *r);
ngx_int_t ngx_http_monitoring_send_health(ngx_http_request_t *r);

ngx_http_monitoring_shctx_t *ngx_http_monitoring_get_shctx(ngx_http_request_t *r);
ngx_http_monitoring_main_conf_t *ngx_http_monitoring_get_main_conf(ngx_http_request_t *r);
ngx_uint_t ngx_http_monitoring_method_index(ngx_uint_t method);
ngx_uint_t ngx_http_monitoring_latency_bucket(ngx_msec_t ms);
ngx_uint_t ngx_http_monitoring_size_bucket(off_t bytes);
double ngx_http_monitoring_percentile(ngx_http_monitoring_shctx_t *sh,
    double percentile);
uint32_t ngx_http_monitoring_hash(ngx_str_t *value);
void ngx_http_monitoring_update_top(ngx_slab_pool_t *shpool,
    ngx_http_monitoring_top_entry_t *entries, ngx_uint_t capacity,
    ngx_str_t *key, ngx_msec_t latency, off_t bytes, ngx_uint_t is_error);

#endif /* _NGX_HTTP_MONITORING_H_INCLUDED_ */

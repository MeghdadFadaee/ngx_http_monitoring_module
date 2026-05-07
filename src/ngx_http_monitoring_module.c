#include "ngx_http_monitoring.h"

static void *ngx_http_monitoring_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_monitoring_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_monitoring_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_monitoring_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_monitoring_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_monitoring_init_process(ngx_cycle_t *cycle);
static void ngx_http_monitoring_exit_process(ngx_cycle_t *cycle);

static char *ngx_http_monitoring_access_rule(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_int_t ngx_http_monitoring_content_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_monitoring_log_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_monitoring_authorize(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf,
    ngx_http_monitoring_endpoint_e endpoint);
static ngx_int_t ngx_http_monitoring_preflight(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf);
static void ngx_http_monitoring_add_cors(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf);
static ngx_int_t ngx_http_monitoring_plain_response(ngx_http_request_t *r,
    ngx_uint_t status, ngx_str_t *body);
static ngx_int_t ngx_http_monitoring_match_access(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf);
static ngx_int_t ngx_http_monitoring_check_basic_auth(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf);
static ngx_int_t ngx_http_monitoring_check_api_token(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf);
static ngx_int_t ngx_http_monitoring_check_rate_limit(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf);
static ngx_table_elt_t *ngx_http_monitoring_find_header(ngx_http_request_t *r,
    ngx_str_t *name);
static ngx_int_t ngx_http_monitoring_consttime_eq(ngx_str_t *a, ngx_str_t *b);


static ngx_command_t ngx_http_monitoring_commands[] = {

    { ngx_string("monitor"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, enabled),
      NULL },

    { ngx_string("monitor_dashboard"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, dashboard),
      NULL },

    { ngx_string("monitor_api"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, api),
      NULL },

    { ngx_string("monitor_sse"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, sse),
      NULL },

    { ngx_string("monitor_refresh_interval"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, refresh_interval),
      NULL },

    { ngx_string("monitor_history"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, history),
      NULL },

    { ngx_string("monitor_resolution"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, resolution),
      NULL },

    { ngx_string("monitor_shm_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, shm_size),
      NULL },

    { ngx_string("monitor_collect_system"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, collect_system),
      NULL },

    { ngx_string("monitor_collect_nginx"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, collect_nginx),
      NULL },

    { ngx_string("monitor_collect_network"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, collect_network),
      NULL },

    { ngx_string("monitor_access_log"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, access_log),
      NULL },

    { ngx_string("monitor_max_top_urls"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_monitoring_main_conf_t, max_top_urls),
      NULL },

    { ngx_string("monitor_api_token"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, api_token),
      NULL },

    { ngx_string("monitor_basic_auth"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, basic_auth),
      NULL },

    { ngx_string("monitor_cors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, cors),
      NULL },

    { ngx_string("monitor_rate_limit"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_monitoring_loc_conf_t, rate_limit),
      NULL },

    { ngx_string("monitor_allow"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_monitoring_access_rule,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("monitor_deny"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_monitoring_access_rule,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t ngx_http_monitoring_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_monitoring_init,              /* postconfiguration */

    ngx_http_monitoring_create_main_conf,  /* create main configuration */
    ngx_http_monitoring_init_main_conf,    /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_monitoring_create_loc_conf,   /* create location configuration */
    ngx_http_monitoring_merge_loc_conf     /* merge location configuration */
};


ngx_module_t ngx_http_monitoring_module = {
    NGX_MODULE_V1,
    &ngx_http_monitoring_module_ctx,
    ngx_http_monitoring_commands,
    NGX_HTTP_MODULE,
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_monitoring_init_process,      /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_http_monitoring_exit_process,      /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_http_monitoring_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_monitoring_main_conf_t  *mmcf;

    mmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_monitoring_main_conf_t));
    if (mmcf == NULL) {
        return NULL;
    }

    mmcf->refresh_interval = NGX_CONF_UNSET_MSEC;
    mmcf->history = NGX_CONF_UNSET_MSEC;
    mmcf->resolution = NGX_CONF_UNSET_MSEC;
    mmcf->shm_size = NGX_CONF_UNSET_SIZE;
    mmcf->max_top_urls = NGX_CONF_UNSET_UINT;
    mmcf->collect_system = NGX_CONF_UNSET;
    mmcf->collect_nginx = NGX_CONF_UNSET;
    mmcf->collect_network = NGX_CONF_UNSET;
    mmcf->access_log = NGX_CONF_UNSET;

    return mmcf;
}


static char *
ngx_http_monitoring_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_monitoring_main_conf_t *mmcf = conf;

    ngx_conf_init_msec_value(mmcf->refresh_interval, 1000);
    ngx_conf_init_msec_value(mmcf->history, 300000);
    ngx_conf_init_msec_value(mmcf->resolution, 1000);
    ngx_conf_init_size_value(mmcf->shm_size,
                             NGX_HTTP_MONITORING_DEFAULT_SHM_SIZE);
    ngx_conf_init_uint_value(mmcf->max_top_urls, 100);
    ngx_conf_init_value(mmcf->collect_system, 1);
    ngx_conf_init_value(mmcf->collect_nginx, 1);
    ngx_conf_init_value(mmcf->collect_network, 1);
    ngx_conf_init_value(mmcf->access_log, 1);

    if (mmcf->refresh_interval < NGX_HTTP_MONITORING_MIN_INTERVAL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "monitor_refresh_interval must be at least 100ms");
        return NGX_CONF_ERROR;
    }

    if (mmcf->resolution < NGX_HTTP_MONITORING_MIN_INTERVAL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "monitor_resolution must be at least 100ms");
        return NGX_CONF_ERROR;
    }

    if (mmcf->max_top_urls == 0
        || mmcf->max_top_urls > NGX_HTTP_MONITORING_TOP_URLS_MAX)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "monitor_max_top_urls must be between 1 and %d",
                           NGX_HTTP_MONITORING_TOP_URLS_MAX);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_monitoring_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_monitoring_loc_conf_t  *mlcf;

    mlcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_monitoring_loc_conf_t));
    if (mlcf == NULL) {
        return NULL;
    }

    mlcf->enabled = NGX_CONF_UNSET;
    mlcf->dashboard = NGX_CONF_UNSET;
    mlcf->api = NGX_CONF_UNSET;
    mlcf->sse = NGX_CONF_UNSET;
    mlcf->rate_limit = NGX_CONF_UNSET_UINT;

    return mlcf;
}


static char *
ngx_http_monitoring_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_monitoring_loc_conf_t *prev = parent;
    ngx_http_monitoring_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_value(conf->dashboard, prev->dashboard, 1);
    ngx_conf_merge_value(conf->api, prev->api, 1);
    ngx_conf_merge_value(conf->sse, prev->sse, 1);
    ngx_conf_merge_uint_value(conf->rate_limit, prev->rate_limit, 60);
    ngx_conf_merge_str_value(conf->api_token, prev->api_token, "");
    ngx_conf_merge_str_value(conf->basic_auth, prev->basic_auth, "");
    ngx_conf_merge_str_value(conf->cors, prev->cors, "");

    if (conf->access_rules == NULL) {
        conf->access_rules = prev->access_rules;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_monitoring_access_rule(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_monitoring_loc_conf_t   *mlcf = conf;
    ngx_http_monitoring_access_rule_t *rule;
    ngx_str_t                        *value;
    ngx_int_t                         rc;

    if (mlcf->access_rules == NULL) {
        mlcf->access_rules = ngx_array_create(cf->pool, 4,
                                sizeof(ngx_http_monitoring_access_rule_t));
        if (mlcf->access_rules == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    rule = ngx_array_push(mlcf->access_rules);
    if (rule == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;
    ngx_memzero(rule, sizeof(ngx_http_monitoring_access_rule_t));
    rule->deny = (cmd->name.len == sizeof("monitor_deny") - 1);

    if (value[1].len == 3 && ngx_strncmp(value[1].data, "all", 3) == 0) {
        rule->cidr.family = AF_UNSPEC;
        return NGX_CONF_OK;
    }

    rc = ngx_ptocidr(&value[1], &rule->cidr);
    if (rc == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid CIDR \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    if (rc == NGX_DONE) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "low address bits of \"%V\" are meaningless",
                           &value[1]);
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_monitoring_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt             *h;
    ngx_http_core_main_conf_t       *cmcf;
    ngx_http_monitoring_main_conf_t *mmcf;
    ngx_str_t                        name = ngx_string("ngx_http_monitoring");

    mmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_monitoring_module);

    mmcf->shm_zone = ngx_shared_memory_add(cf, &name, mmcf->shm_size,
                                           &ngx_http_monitoring_module);
    if (mmcf->shm_zone == NULL) {
        return NGX_ERROR;
    }

    mmcf->shm_zone->init = ngx_http_monitoring_init_shm;
    mmcf->shm_zone->data = mmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_monitoring_content_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_monitoring_log_handler;

    return NGX_OK;
}


static ngx_int_t
ngx_http_monitoring_init_process(ngx_cycle_t *cycle)
{
    return ngx_http_monitoring_init_collector(cycle);
}


static void
ngx_http_monitoring_exit_process(ngx_cycle_t *cycle)
{
    ngx_http_monitoring_main_conf_t *mmcf;
    ngx_http_monitoring_shctx_t     *sh;
    ngx_uint_t                       slot;

    mmcf = ngx_http_cycle_get_module_main_conf(cycle,
                                               ngx_http_monitoring_module);
    if (mmcf == NULL) {
        return;
    }

    mmcf->collector_active = 0;
    if (mmcf->collector_event.timer_set) {
        ngx_del_timer(&mmcf->collector_event);
    }

    sh = (ngx_http_monitoring_shctx_t *) mmcf->sh;
    if (sh == NULL || ngx_process_slot < 0) {
        return;
    }

    slot = ((ngx_uint_t) ngx_process_slot) % NGX_HTTP_MONITORING_WORKERS_MAX;
    sh->workers[slot].active = 0;
    sh->workers[slot].last_seen = ngx_time();
}


static ngx_int_t
ngx_http_monitoring_content_handler(ngx_http_request_t *r)
{
    ngx_int_t                         rc;
    ngx_http_monitoring_endpoint_e    endpoint;
    ngx_http_monitoring_loc_conf_t   *mlcf;

    endpoint = ngx_http_monitoring_match_endpoint(r);
    if (endpoint == NGX_HTTP_MONITORING_EP_NONE) {
        return NGX_DECLINED;
    }

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_monitoring_module);
    if (mlcf == NULL || !mlcf->enabled) {
        return NGX_DECLINED;
    }

    if (r->method == NGX_HTTP_OPTIONS) {
        return ngx_http_monitoring_preflight(r, mlcf);
    }

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_DASHBOARD && !mlcf->dashboard) {
        return NGX_HTTP_NOT_FOUND;
    }

    if (endpoint >= NGX_HTTP_MONITORING_EP_API_FULL
        && endpoint <= NGX_HTTP_MONITORING_EP_API_REQUESTS
        && !mlcf->api)
    {
        return NGX_HTTP_NOT_FOUND;
    }

    if (endpoint == NGX_HTTP_MONITORING_EP_LIVE && !mlcf->sse) {
        return NGX_HTTP_NOT_FOUND;
    }

    rc = ngx_http_monitoring_authorize(r, mlcf, endpoint);
    if (rc != NGX_OK) {
        return rc;
    }

    ngx_http_monitoring_add_cors(r, mlcf);

    switch (endpoint) {
    case NGX_HTTP_MONITORING_EP_DASHBOARD:
        return ngx_http_monitoring_send_dashboard(r);
    case NGX_HTTP_MONITORING_EP_LIVE:
        return ngx_http_monitoring_send_sse(r);
    case NGX_HTTP_MONITORING_EP_PROMETHEUS:
        return ngx_http_monitoring_send_prometheus(r);
    case NGX_HTTP_MONITORING_EP_HEALTH:
        return ngx_http_monitoring_send_health(r);
    default:
        return ngx_http_monitoring_send_json(r, endpoint);
    }
}


static ngx_int_t
ngx_http_monitoring_log_handler(ngx_http_request_t *r)
{
    ngx_http_monitoring_main_conf_t *mmcf;

    mmcf = ngx_http_get_module_main_conf(r, ngx_http_monitoring_module);
    if (mmcf == NULL || !mmcf->access_log || mmcf->sh == NULL) {
        return NGX_OK;
    }

    if (ngx_http_monitoring_match_endpoint(r) != NGX_HTTP_MONITORING_EP_NONE) {
        return NGX_OK;
    }

    ngx_http_monitoring_account_request(r);

    return NGX_OK;
}


ngx_http_monitoring_endpoint_e
ngx_http_monitoring_match_endpoint(ngx_http_request_t *r)
{
    ngx_str_t *u = &r->uri;

    if (u->len == sizeof("/monitor") - 1
        && ngx_strncmp(u->data, "/monitor", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_DASHBOARD;
    }

    if (u->len == sizeof("/monitor/") - 1
        && ngx_strncmp(u->data, "/monitor/", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_DASHBOARD;
    }

    if (u->len == sizeof("/monitor/live") - 1
        && ngx_strncmp(u->data, "/monitor/live", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_LIVE;
    }

    if (u->len == sizeof("/monitor/metrics") - 1
        && ngx_strncmp(u->data, "/monitor/metrics", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_PROMETHEUS;
    }

    if (u->len == sizeof("/monitor/health") - 1
        && ngx_strncmp(u->data, "/monitor/health", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_HEALTH;
    }

    if (u->len == sizeof("/monitor/api") - 1
        && ngx_strncmp(u->data, "/monitor/api", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_FULL;
    }

    if (u->len == sizeof("/monitor/api/system") - 1
        && ngx_strncmp(u->data, "/monitor/api/system", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_SYSTEM;
    }

    if (u->len == sizeof("/monitor/api/nginx") - 1
        && ngx_strncmp(u->data, "/monitor/api/nginx", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_NGINX;
    }

    if (u->len == sizeof("/monitor/api/network") - 1
        && ngx_strncmp(u->data, "/monitor/api/network", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_NETWORK;
    }

    if (u->len == sizeof("/monitor/api/disk") - 1
        && ngx_strncmp(u->data, "/monitor/api/disk", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_DISK;
    }

    if (u->len == sizeof("/monitor/api/processes") - 1
        && ngx_strncmp(u->data, "/monitor/api/processes", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_PROCESSES;
    }

    if (u->len == sizeof("/monitor/api/upstreams") - 1
        && ngx_strncmp(u->data, "/monitor/api/upstreams", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_UPSTREAMS;
    }

    if (u->len == sizeof("/monitor/api/connections") - 1
        && ngx_strncmp(u->data, "/monitor/api/connections", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_CONNECTIONS;
    }

    if (u->len == sizeof("/monitor/api/requests") - 1
        && ngx_strncmp(u->data, "/monitor/api/requests", u->len) == 0)
    {
        return NGX_HTTP_MONITORING_EP_API_REQUESTS;
    }

    return NGX_HTTP_MONITORING_EP_NONE;
}


static ngx_int_t
ngx_http_monitoring_authorize(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf,
    ngx_http_monitoring_endpoint_e endpoint)
{
    ngx_str_t forbidden = ngx_string("forbidden\n");
    ngx_str_t unauthorized = ngx_string("unauthorized\n");
    ngx_str_t limited = ngx_string("rate limited\n");

    if (ngx_http_monitoring_match_access(r, mlcf) != NGX_OK) {
        return ngx_http_monitoring_plain_response(r, NGX_HTTP_FORBIDDEN,
                                                  &forbidden);
    }

    if (ngx_http_monitoring_check_basic_auth(r, mlcf) != NGX_OK) {
        r->headers_out.www_authenticate =
            ngx_list_push(&r->headers_out.headers);
        if (r->headers_out.www_authenticate == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        r->headers_out.www_authenticate->hash = 1;
        ngx_str_set(&r->headers_out.www_authenticate->key,
                    "WWW-Authenticate");
        ngx_str_set(&r->headers_out.www_authenticate->value,
                    "Basic realm=\"nginx monitoring\"");

        return ngx_http_monitoring_plain_response(r, NGX_HTTP_UNAUTHORIZED,
                                                  &unauthorized);
    }

    if (endpoint != NGX_HTTP_MONITORING_EP_DASHBOARD
        && ngx_http_monitoring_check_api_token(r, mlcf) != NGX_OK)
    {
        return ngx_http_monitoring_plain_response(r, NGX_HTTP_UNAUTHORIZED,
                                                  &unauthorized);
    }

    if (ngx_http_monitoring_check_rate_limit(r, mlcf) != NGX_OK) {
        return ngx_http_monitoring_plain_response(r, 429, &limited);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_monitoring_preflight(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf)
{
    ngx_http_monitoring_add_cors(r, mlcf);

    r->headers_out.status = NGX_HTTP_NO_CONTENT;
    r->headers_out.content_length_n = 0;
    r->header_only = 1;

    return ngx_http_send_header(r);
}


static void
ngx_http_monitoring_add_cors(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf)
{
    ngx_table_elt_t *h;

    if (mlcf->cors.len == 0
        || (mlcf->cors.len == 3
            && ngx_strncmp(mlcf->cors.data, "off", 3) == 0))
    {
        return;
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return;
    }
    h->hash = 1;
    ngx_str_set(&h->key, "Access-Control-Allow-Origin");
    h->value = mlcf->cors;

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return;
    }
    h->hash = 1;
    ngx_str_set(&h->key, "Access-Control-Allow-Methods");
    ngx_str_set(&h->value, "GET, HEAD, OPTIONS");

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return;
    }
    h->hash = 1;
    ngx_str_set(&h->key, "Access-Control-Allow-Headers");
    ngx_str_set(&h->value, "Authorization, X-Monitor-Token, Content-Type");
}


static ngx_int_t
ngx_http_monitoring_plain_response(ngx_http_request_t *r, ngx_uint_t status,
    ngx_str_t *body)
{
    ngx_buf_t    *b;
    ngx_chain_t   out;

    ngx_http_discard_request_body(r);

    r->headers_out.status = status;
    r->headers_out.content_length_n = body->len;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    if (r->method == NGX_HTTP_HEAD) {
        r->header_only = 1;
        return ngx_http_send_header(r);
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = body->data;
    b->last = body->data + body->len;
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    ngx_http_send_header(r);
    return ngx_http_output_filter(r, &out);
}


static ngx_int_t
ngx_http_monitoring_match_access(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf)
{
    ngx_uint_t                         i, j;
    ngx_http_monitoring_access_rule_t *rule;
    struct sockaddr                   *sa;
    struct sockaddr_in                *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6               *sin6;
#endif
    u_char                            *addr;
    u_char                            *mask;
    u_char                            *cidr;

    if (mlcf->access_rules == NULL || mlcf->access_rules->nelts == 0) {
        return NGX_OK;
    }

    rule = mlcf->access_rules->elts;
    sa = r->connection->sockaddr;

    for (i = 0; i < mlcf->access_rules->nelts; i++) {
        if (rule[i].cidr.family == AF_UNSPEC) {
            return rule[i].deny ? NGX_DECLINED : NGX_OK;
        }

        if (sa->sa_family != (ngx_int_t) rule[i].cidr.family) {
            continue;
        }

        if (sa->sa_family == AF_INET) {
            sin = (struct sockaddr_in *) sa;
            if ((sin->sin_addr.s_addr & rule[i].cidr.u.in.mask)
                == rule[i].cidr.u.in.addr)
            {
                return rule[i].deny ? NGX_DECLINED : NGX_OK;
            }
            continue;
        }

#if (NGX_HAVE_INET6)
        if (sa->sa_family == AF_INET6) {
            sin6 = (struct sockaddr_in6 *) sa;
            addr = sin6->sin6_addr.s6_addr;
            mask = rule[i].cidr.u.in6.mask.s6_addr;
            cidr = rule[i].cidr.u.in6.addr.s6_addr;

            for (j = 0; j < 16; j++) {
                if ((addr[j] & mask[j]) != cidr[j]) {
                    break;
                }
            }

            if (j == 16) {
                return rule[i].deny ? NGX_DECLINED : NGX_OK;
            }
        }
#endif
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_monitoring_check_basic_auth(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf)
{
    ngx_table_elt_t *authorization;
    ngx_str_t        encoded, decoded, expected;

    if (mlcf->basic_auth.len == 0
        || (mlcf->basic_auth.len == 3
            && ngx_strncmp(mlcf->basic_auth.data, "off", 3) == 0))
    {
        return NGX_OK;
    }

    authorization = r->headers_in.authorization;
    if (authorization == NULL || authorization->value.len <= 6
        || ngx_strncasecmp(authorization->value.data,
                           (u_char *) "Basic ", 6) != 0)
    {
        return NGX_DECLINED;
    }

    encoded.data = authorization->value.data + 6;
    encoded.len = authorization->value.len - 6;
    decoded.len = ngx_base64_decoded_length(encoded.len);
    decoded.data = ngx_pnalloc(r->pool, decoded.len);
    if (decoded.data == NULL) {
        return NGX_DECLINED;
    }

    if (ngx_decode_base64(&decoded, &encoded) != NGX_OK) {
        return NGX_DECLINED;
    }

    expected = mlcf->basic_auth;
    return ngx_http_monitoring_consttime_eq(&decoded, &expected);
}


static ngx_int_t
ngx_http_monitoring_check_api_token(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf)
{
    ngx_table_elt_t *h;
    ngx_str_t        header = ngx_string("X-Monitor-Token");
    ngx_str_t        token, bearer, query;

    if (mlcf->api_token.len == 0) {
        return NGX_OK;
    }

    h = ngx_http_monitoring_find_header(r, &header);
    if (h != NULL) {
        token = h->value;
        if (ngx_http_monitoring_consttime_eq(&token, &mlcf->api_token)
            == NGX_OK)
        {
            return NGX_OK;
        }
    }

    h = r->headers_in.authorization;
    if (h != NULL && h->value.len > 7
        && ngx_strncasecmp(h->value.data, (u_char *) "Bearer ", 7) == 0)
    {
        bearer.data = h->value.data + 7;
        bearer.len = h->value.len - 7;

        if (ngx_http_monitoring_consttime_eq(&bearer, &mlcf->api_token)
            == NGX_OK)
        {
            return NGX_OK;
        }
    }

    if (ngx_http_arg(r, (u_char *) "token", 5, &query) == NGX_OK
        && ngx_http_monitoring_consttime_eq(&query, &mlcf->api_token)
           == NGX_OK)
    {
        return NGX_OK;
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_monitoring_check_rate_limit(ngx_http_request_t *r,
    ngx_http_monitoring_loc_conf_t *mlcf)
{
    time_t                            now;
    ngx_atomic_uint_t                 count;
    ngx_http_monitoring_shctx_t      *sh;
    ngx_http_monitoring_main_conf_t  *mmcf;

    if (mlcf->rate_limit == 0) {
        return NGX_OK;
    }

    sh = ngx_http_monitoring_get_shctx(r);
    mmcf = ngx_http_monitoring_get_main_conf(r);
    if (sh == NULL || mmcf == NULL || mmcf->shpool == NULL) {
        return NGX_OK;
    }

    now = ngx_time();

    if (sh->rate_limit_window != (ngx_atomic_uint_t) now) {
        ngx_shmtx_lock(&mmcf->shpool->mutex);
        if (sh->rate_limit_window != (ngx_atomic_uint_t) now) {
            sh->rate_limit_window = (ngx_atomic_uint_t) now;
            sh->rate_limit_count = 0;
        }
        ngx_shmtx_unlock(&mmcf->shpool->mutex);
    }

    count = ngx_atomic_fetch_add(&sh->rate_limit_count, 1) + 1;
    if (count > mlcf->rate_limit) {
        ngx_atomic_fetch_add(&sh->connections.rate_limited, 1);
        return NGX_DECLINED;
    }

    return NGX_OK;
}


static ngx_table_elt_t *
ngx_http_monitoring_find_header(ngx_http_request_t *r, ngx_str_t *name)
{
    ngx_list_part_t  *part;
    ngx_table_elt_t  *h;
    ngx_uint_t        i;

    part = &r->headers_in.headers.part;
    h = part->elts;

    for (i = 0; /* void */; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].key.len == name->len
            && ngx_strncasecmp(h[i].key.data, name->data, name->len) == 0)
        {
            return &h[i];
        }
    }

    return NULL;
}


static ngx_int_t
ngx_http_monitoring_consttime_eq(ngx_str_t *a, ngx_str_t *b)
{
    size_t      i, max;
    ngx_uint_t  diff;

    max = (a->len > b->len) ? a->len : b->len;
    diff = (ngx_uint_t) (a->len ^ b->len);

    for (i = 0; i < max; i++) {
        diff |= (ngx_uint_t)
                ((i < a->len ? a->data[i] : 0)
                 ^ (i < b->len ? b->data[i] : 0));
    }

    return diff == 0 ? NGX_OK : NGX_DECLINED;
}

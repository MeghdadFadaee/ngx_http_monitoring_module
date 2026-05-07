#include "ngx_http_monitoring.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <mntent.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static ssize_t ngx_http_monitoring_read_file(const char *path, u_char *buf,
    size_t size);
static void ngx_http_monitoring_collect_cpu(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_load(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_meminfo(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_uptime(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_processes(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_self_status(
    ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_tcp(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_sockstat(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_network(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_diskstats(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_filesystems(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_nginx(ngx_http_monitoring_shctx_t *sh);
static void ngx_http_monitoring_collect_history(
    ngx_http_monitoring_main_conf_t *mmcf, ngx_http_monitoring_shctx_t *sh);
static ngx_atomic_uint_t ngx_http_monitoring_sum_network(
    ngx_http_monitoring_shctx_t *sh, ngx_uint_t tx);
static ngx_atomic_uint_t ngx_http_monitoring_sum_disk(
    ngx_http_monitoring_shctx_t *sh, ngx_uint_t write);
static ngx_atomic_uint_t ngx_http_monitoring_delta(ngx_atomic_uint_t now,
    ngx_atomic_uint_t prev);
static ngx_uint_t ngx_http_monitoring_skip_fs(const char *type);


ngx_int_t
ngx_http_monitoring_init_collector(ngx_cycle_t *cycle)
{
    ngx_http_monitoring_main_conf_t *mmcf;
    ngx_http_monitoring_shctx_t     *sh;
    ngx_uint_t                       slot;

    if (ngx_process != NGX_PROCESS_WORKER) {
        return NGX_OK;
    }

    mmcf = ngx_http_cycle_get_module_main_conf(cycle,
                                               ngx_http_monitoring_module);
    if (mmcf == NULL || mmcf->sh == NULL) {
        return NGX_OK;
    }

    sh = (ngx_http_monitoring_shctx_t *) mmcf->sh;

    if (ngx_process_slot >= 0) {
        slot = ((ngx_uint_t) ngx_process_slot)
               % NGX_HTTP_MONITORING_WORKERS_MAX;
        sh->workers[slot].pid = ngx_pid;
        sh->workers[slot].active = 1;
        sh->workers[slot].last_seen = ngx_time();
    }

    ngx_memzero(&mmcf->collector_event, sizeof(ngx_event_t));
    mmcf->collector_event.handler = ngx_http_monitoring_collect;
    mmcf->collector_event.log = cycle->log;
    mmcf->collector_event.data = cycle;
    mmcf->collector_active = 1;

    ngx_add_timer(&mmcf->collector_event,
                  50 + ((ngx_process_slot >= 0 ? ngx_process_slot : 0) % 16)
                       * 25);

    return NGX_OK;
}


void
ngx_http_monitoring_collect(ngx_event_t *ev)
{
    ngx_cycle_t                      *cycle;
    ngx_http_monitoring_main_conf_t  *mmcf;
    ngx_http_monitoring_shctx_t      *sh;
    ngx_uint_t                        slot;

    cycle = ev->data;
    mmcf = ngx_http_cycle_get_module_main_conf(cycle,
                                               ngx_http_monitoring_module);
    if (mmcf == NULL || mmcf->sh == NULL) {
        return;
    }

    sh = (ngx_http_monitoring_shctx_t *) mmcf->sh;

    if (ngx_process_slot >= 0) {
        slot = ((ngx_uint_t) ngx_process_slot)
               % NGX_HTTP_MONITORING_WORKERS_MAX;
        sh->workers[slot].pid = ngx_pid;
        sh->workers[slot].active = 1;
        sh->workers[slot].last_seen = ngx_time();
    }

    ngx_http_monitoring_collect_self_status(sh);

    if (ngx_atomic_cmp_set(&sh->collector_lock, 0, (ngx_atomic_uint_t) ngx_pid))
    {
        if (mmcf->collect_nginx) {
            ngx_http_monitoring_collect_nginx(sh);
        }

        if (mmcf->collect_system) {
            ngx_http_monitoring_collect_cpu(sh);
            ngx_http_monitoring_collect_load(sh);
            ngx_http_monitoring_collect_meminfo(sh);
            ngx_http_monitoring_collect_uptime(sh);
            ngx_http_monitoring_collect_processes(sh);
            ngx_http_monitoring_collect_tcp(sh);
            ngx_http_monitoring_collect_sockstat(sh);
            ngx_http_monitoring_collect_diskstats(sh);
            ngx_http_monitoring_collect_filesystems(sh);
        }

        if (mmcf->collect_network) {
            ngx_http_monitoring_collect_network(sh);
        }

        ngx_http_monitoring_collect_history(mmcf, sh);

        sh->generation++;
        sh->collector_lock = 0;
    }

    if (!ngx_exiting && mmcf->collector_active) {
        ngx_add_timer(ev, mmcf->refresh_interval);
    }
}


static ssize_t
ngx_http_monitoring_read_file(const char *path, u_char *buf, size_t size)
{
    int      fd;
    ssize_t  n;

    if (size == 0) {
        return -1;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        return -1;
    }

    n = read(fd, buf, size - 1);
    close(fd);

    if (n < 0) {
        return -1;
    }

    buf[n] = '\0';
    return n;
}


static void
ngx_http_monitoring_collect_cpu(ngx_http_monitoring_shctx_t *sh)
{
    u_char              buf[32768];
    char               *line, *next;
    unsigned long long  user, nice, system, idle, iowait, irq, softirq, steal;
    uint64_t            total, idle_all, delta_total, delta_idle;
    ngx_uint_t          cores;

    if (ngx_http_monitoring_read_file("/proc/stat", buf, sizeof(buf)) <= 0) {
        return;
    }

    user = nice = system = idle = iowait = irq = softirq = steal = 0;

    if (sscanf((char *) buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal)
        < 4)
    {
        return;
    }

    total = user + nice + system + idle + iowait + irq + softirq + steal;
    idle_all = idle + iowait;

    if (sh->prev_cpu_total > 0 && total > sh->prev_cpu_total) {
        delta_total = total - sh->prev_cpu_total;
        delta_idle = idle_all - sh->prev_cpu_idle;
        if (delta_total > 0) {
            sh->system.usage_milli =
                (ngx_atomic_uint_t) (((delta_total - delta_idle) * 100000)
                                     / delta_total);
        }
    }

    sh->prev_cpu_total = total;
    sh->prev_cpu_idle = idle_all;

    cores = 0;
    line = (char *) buf;
    while (line != NULL && *line != '\0') {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        if (line[0] == 'c' && line[1] == 'p' && line[2] == 'u'
            && isdigit((unsigned char) line[3]))
        {
            cores++;
        }

        line = next;
    }

    if (cores > 0) {
        sh->system.cores = cores;
    }
}


static void
ngx_http_monitoring_collect_load(ngx_http_monitoring_shctx_t *sh)
{
    u_char  buf[256];
    double  l1, l5, l15;

    if (ngx_http_monitoring_read_file("/proc/loadavg", buf, sizeof(buf)) <= 0) {
        return;
    }

    if (sscanf((char *) buf, "%lf %lf %lf", &l1, &l5, &l15) != 3) {
        return;
    }

    sh->system.load1_milli = (ngx_atomic_uint_t) (l1 * 1000.0);
    sh->system.load5_milli = (ngx_atomic_uint_t) (l5 * 1000.0);
    sh->system.load15_milli = (ngx_atomic_uint_t) (l15 * 1000.0);
}


static void
ngx_http_monitoring_collect_meminfo(ngx_http_monitoring_shctx_t *sh)
{
    u_char              buf[8192];
    char               *line, *next;
    unsigned long long  value;

    if (ngx_http_monitoring_read_file("/proc/meminfo", buf, sizeof(buf)) <= 0) {
        return;
    }

    line = (char *) buf;
    while (line != NULL && *line != '\0') {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
            sh->system.mem_total = (ngx_atomic_uint_t) (value * 1024);
        } else if (sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            sh->system.mem_available = (ngx_atomic_uint_t) (value * 1024);
        } else if (sscanf(line, "MemFree: %llu kB", &value) == 1) {
            sh->system.mem_free = (ngx_atomic_uint_t) (value * 1024);
        } else if (sscanf(line, "SwapTotal: %llu kB", &value) == 1) {
            sh->system.swap_total = (ngx_atomic_uint_t) (value * 1024);
        } else if (sscanf(line, "SwapFree: %llu kB", &value) == 1) {
            sh->system.swap_free = (ngx_atomic_uint_t) (value * 1024);
        }

        line = next;
    }
}


static void
ngx_http_monitoring_collect_uptime(ngx_http_monitoring_shctx_t *sh)
{
    u_char  buf[256];
    double  uptime;

    if (ngx_http_monitoring_read_file("/proc/uptime", buf, sizeof(buf)) <= 0) {
        return;
    }

    if (sscanf((char *) buf, "%lf", &uptime) == 1) {
        sh->system.uptime = (ngx_atomic_uint_t) uptime;
    }
}


static void
ngx_http_monitoring_collect_processes(ngx_http_monitoring_shctx_t *sh)
{
    DIR            *dir;
    struct dirent  *de;
    ngx_uint_t      count;

    dir = opendir("/proc");
    if (dir == NULL) {
        return;
    }

    count = 0;
    while ((de = readdir(dir)) != NULL) {
        if (isdigit((unsigned char) de->d_name[0])) {
            count++;
        }
    }

    closedir(dir);
    sh->system.process_count = count;
}


static void
ngx_http_monitoring_collect_self_status(ngx_http_monitoring_shctx_t *sh)
{
    u_char                              buf[8192];
    char                               *line, *next;
    unsigned long long                  value;
    ngx_uint_t                          slot;
    ngx_http_monitoring_worker_metric_t *w;

    if (ngx_process_slot < 0) {
        return;
    }

    if (ngx_http_monitoring_read_file("/proc/self/status", buf, sizeof(buf))
        <= 0)
    {
        return;
    }

    slot = ((ngx_uint_t) ngx_process_slot) % NGX_HTTP_MONITORING_WORKERS_MAX;
    w = &sh->workers[slot];

    line = (char *) buf;
    while (line != NULL && *line != '\0') {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        if (sscanf(line, "VmSize: %llu kB", &value) == 1) {
            w->vm_size = (ngx_atomic_uint_t) (value * 1024);
        } else if (sscanf(line, "VmRSS: %llu kB", &value) == 1) {
            w->vm_rss = (ngx_atomic_uint_t) (value * 1024);
        } else if (sscanf(line, "voluntary_ctxt_switches: %llu", &value)
                   == 1)
        {
            w->voluntary_ctxt = (ngx_atomic_uint_t) value;
        } else if (sscanf(line, "nonvoluntary_ctxt_switches: %llu", &value)
                   == 1)
        {
            w->nonvoluntary_ctxt = (ngx_atomic_uint_t) value;
        }

        line = next;
    }
}


static void
ngx_http_monitoring_collect_tcp(ngx_http_monitoring_shctx_t *sh)
{
    FILE      *fp;
    char       line[512];
    unsigned   state;
    ngx_uint_t established, listen;
    const char *paths[2] = { "/proc/net/tcp", "/proc/net/tcp6" };
    ngx_uint_t i;

    established = 0;
    listen = 0;

    for (i = 0; i < 2; i++) {
        fp = fopen(paths[i], "r");
        if (fp == NULL) {
            continue;
        }

        (void) fgets(line, sizeof(line), fp);

        while (fgets(line, sizeof(line), fp) != NULL) {
            state = 0;
            if (sscanf(line, " %*u: %*64s %*64s %x", &state) == 1) {
                if (state == 0x01) {
                    established++;
                } else if (state == 0x0a) {
                    listen++;
                }
            }
        }

        fclose(fp);
    }

    sh->system.tcp_established = established;
    sh->system.tcp_listen = listen;
}


static void
ngx_http_monitoring_collect_sockstat(ngx_http_monitoring_shctx_t *sh)
{
    u_char    buf[4096];
    char     *line, *next;
    unsigned  used, tcp, udp;

    if (ngx_http_monitoring_read_file("/proc/net/sockstat", buf, sizeof(buf))
        <= 0)
    {
        return;
    }

    line = (char *) buf;
    while (line != NULL && *line != '\0') {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        if (sscanf(line, "sockets: used %u", &used) == 1) {
            sh->system.sockets_used = used;
        } else if (sscanf(line, "TCP: inuse %u", &tcp) == 1) {
            sh->system.sockets_tcp = tcp;
        } else if (sscanf(line, "UDP: inuse %u", &udp) == 1) {
            sh->system.sockets_udp = udp;
        }

        line = next;
    }
}


static void
ngx_http_monitoring_collect_network(ngx_http_monitoring_shctx_t *sh)
{
    u_char              buf[32768];
    char               *line, *next, *colon, name[NGX_HTTP_MONITORING_NAME_LEN];
    unsigned long long  rx_bytes, rx_packets, rx_errs;
    unsigned long long  tx_bytes, tx_packets, tx_errs;
    ngx_uint_t          count, i;
    struct ifaddrs     *ifaddr, *ifa;

    if (ngx_http_monitoring_read_file("/proc/net/dev", buf, sizeof(buf)) <= 0) {
        return;
    }

    count = 0;
    line = (char *) buf;

    while (line != NULL && *line != '\0') {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        colon = strchr(line, ':');
        if (colon != NULL && count < NGX_HTTP_MONITORING_IFACES_MAX) {
            *colon = '\0';
            while (*line == ' ') {
                line++;
            }

            ngx_memzero(name, sizeof(name));
            if (sscanf(line, "%63s", name) != 1) {
                line = next;
                continue;
            }

            rx_bytes = rx_packets = rx_errs = 0;
            tx_bytes = tx_packets = tx_errs = 0;
            if (sscanf(colon + 1,
                       "%llu %llu %llu %*u %*u %*u %*u %*u "
                       "%llu %llu %llu",
                       &rx_bytes, &rx_packets, &rx_errs,
                       &tx_bytes, &tx_packets, &tx_errs) == 6)
            {
                ngx_cpystrn(sh->ifaces[count].name, (u_char *) name,
                            NGX_HTTP_MONITORING_NAME_LEN);
                sh->ifaces[count].rx_bytes = (ngx_atomic_uint_t) rx_bytes;
                sh->ifaces[count].tx_bytes = (ngx_atomic_uint_t) tx_bytes;
                sh->ifaces[count].rx_packets = (ngx_atomic_uint_t) rx_packets;
                sh->ifaces[count].tx_packets = (ngx_atomic_uint_t) tx_packets;
                sh->ifaces[count].rx_errors = (ngx_atomic_uint_t) rx_errs;
                sh->ifaces[count].tx_errors = (ngx_atomic_uint_t) tx_errs;
                sh->ifaces[count].flags = 0;
                count++;
            }
        }

        line = next;
    }

    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_name == NULL) {
                continue;
            }

            for (i = 0; i < count; i++) {
                if (ngx_strncmp(sh->ifaces[i].name, ifa->ifa_name,
                                NGX_HTTP_MONITORING_NAME_LEN) == 0)
                {
                    sh->ifaces[i].flags = ifa->ifa_flags;
                    break;
                }
            }
        }

        freeifaddrs(ifaddr);
    }

    sh->iface_count = count;
}


static void
ngx_http_monitoring_collect_diskstats(ngx_http_monitoring_shctx_t *sh)
{
    u_char              buf[65536];
    char               *line, *next, name[NGX_HTTP_MONITORING_NAME_LEN];
    unsigned            major, minor;
    unsigned long long  reads, sectors_read, writes, sectors_written, io_ms;
    ngx_uint_t          count;

    if (ngx_http_monitoring_read_file("/proc/diskstats", buf, sizeof(buf))
        <= 0)
    {
        return;
    }

    count = 0;
    line = (char *) buf;

    while (line != NULL && *line != '\0'
           && count < NGX_HTTP_MONITORING_DISKS_MAX)
    {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }

        ngx_memzero(name, sizeof(name));
        reads = sectors_read = writes = sectors_written = io_ms = 0;

        if (sscanf(line,
                   " %u %u %63s %llu %*u %llu %*u %llu %*u %llu %*u %*u %llu",
                   &major, &minor, name, &reads, &sectors_read,
                   &writes, &sectors_written, &io_ms) == 8)
        {
            (void) major;
            (void) minor;

            if (ngx_strncmp(name, "loop", 4) != 0
                && ngx_strncmp(name, "ram", 3) != 0)
            {
                ngx_cpystrn(sh->disks[count].name, (u_char *) name,
                            NGX_HTTP_MONITORING_NAME_LEN);
                sh->disks[count].reads = (ngx_atomic_uint_t) reads;
                sh->disks[count].writes = (ngx_atomic_uint_t) writes;
                sh->disks[count].read_bytes =
                    (ngx_atomic_uint_t) (sectors_read * 512);
                sh->disks[count].write_bytes =
                    (ngx_atomic_uint_t) (sectors_written * 512);
                sh->disks[count].io_ms = (ngx_atomic_uint_t) io_ms;
                count++;
            }
        }

        line = next;
    }

    sh->disk_count = count;
}


static void
ngx_http_monitoring_collect_filesystems(ngx_http_monitoring_shctx_t *sh)
{
    FILE             *fp;
    struct mntent    *mnt;
    struct statvfs    st;
    ngx_uint_t        count;
    uint64_t          total, free_bytes, avail, used;

    fp = setmntent("/proc/mounts", "r");
    if (fp == NULL) {
        return;
    }

    count = 0;
    while ((mnt = getmntent(fp)) != NULL
           && count < NGX_HTTP_MONITORING_FILESYSTEMS_MAX)
    {
        if (ngx_http_monitoring_skip_fs(mnt->mnt_type)) {
            continue;
        }

        if (statvfs(mnt->mnt_dir, &st) != 0 || st.f_blocks == 0) {
            continue;
        }

        total = (uint64_t) st.f_blocks * st.f_frsize;
        free_bytes = (uint64_t) st.f_bfree * st.f_frsize;
        avail = (uint64_t) st.f_bavail * st.f_frsize;
        used = total - free_bytes;

        ngx_cpystrn(sh->filesystems[count].path, (u_char *) mnt->mnt_dir,
                    NGX_HTTP_MONITORING_KEY_LEN);
        ngx_cpystrn(sh->filesystems[count].type, (u_char *) mnt->mnt_type,
                    NGX_HTTP_MONITORING_NAME_LEN);
        sh->filesystems[count].total = (ngx_atomic_uint_t) total;
        sh->filesystems[count].used = (ngx_atomic_uint_t) used;
        sh->filesystems[count].free = (ngx_atomic_uint_t) free_bytes;
        sh->filesystems[count].avail = (ngx_atomic_uint_t) avail;
        sh->filesystems[count].files = (ngx_atomic_uint_t) st.f_files;
        sh->filesystems[count].files_free = (ngx_atomic_uint_t) st.f_ffree;

        count++;
    }

    endmntent(fp);
    sh->fs_count = count;
}


static void
ngx_http_monitoring_collect_nginx(ngx_http_monitoring_shctx_t *sh)
{
#if (NGX_STAT_STUB)
    sh->connections.accepted = *ngx_stat_accepted;
    sh->connections.handled = *ngx_stat_handled;
    sh->connections.total = *ngx_stat_active;
    sh->connections.active = *ngx_stat_active;
    sh->connections.reading = *ngx_stat_reading;
    sh->connections.writing = *ngx_stat_writing;
    sh->connections.waiting = *ngx_stat_waiting;
#else
    (void) sh;
#endif
}


static void
ngx_http_monitoring_collect_history(ngx_http_monitoring_main_conf_t *mmcf,
    ngx_http_monitoring_shctx_t *sh)
{
    ngx_time_t                         *tp;
    ngx_msec_t                          now_msec;
    ngx_atomic_uint_t                   req, resp, net_rx, net_tx;
    ngx_atomic_uint_t                   disk_read, disk_write, prev_msec;
    ngx_atomic_uint_t                   dt, rps_milli, resp_milli;
    ngx_atomic_uint_t                   net_rx_rate, net_tx_rate;
    ngx_atomic_uint_t                   disk_read_rate, disk_write_rate;
    ngx_uint_t                          pos, capacity;
    ngx_http_monitoring_history_sample_t *sample;
    double                              mem_pct, swap_pct;

    tp = ngx_timeofday();
    now_msec = (ngx_msec_t) (tp->sec * 1000 + tp->msec);

    if (sh->prev_history_msec != 0
        && now_msec - sh->prev_history_msec < mmcf->resolution)
    {
        return;
    }

    req = sh->requests.total;
    resp = sh->requests.responses;
    net_rx = ngx_http_monitoring_sum_network(sh, 0);
    net_tx = ngx_http_monitoring_sum_network(sh, 1);
    disk_read = ngx_http_monitoring_sum_disk(sh, 0);
    disk_write = ngx_http_monitoring_sum_disk(sh, 1);

    prev_msec = sh->prev_collect_msec;
    dt = (prev_msec == 0 || now_msec <= prev_msec)
         ? mmcf->refresh_interval
         : now_msec - prev_msec;
    if (dt == 0) {
        dt = 1;
    }

    rps_milli = (ngx_http_monitoring_delta(req, sh->prev_req_total)
                 * 1000000) / dt;
    resp_milli = (ngx_http_monitoring_delta(resp, sh->prev_resp_total)
                  * 1000000) / dt;
    net_rx_rate = (ngx_http_monitoring_delta(net_rx, sh->prev_net_rx)
                   * 1000) / dt;
    net_tx_rate = (ngx_http_monitoring_delta(net_tx, sh->prev_net_tx)
                   * 1000) / dt;
    disk_read_rate = (ngx_http_monitoring_delta(disk_read,
                      sh->prev_disk_read) * 1000) / dt;
    disk_write_rate = (ngx_http_monitoring_delta(disk_write,
                       sh->prev_disk_write) * 1000) / dt;

    if (sh->ewma_rps_milli == 0) {
        sh->ewma_rps_milli = rps_milli;
        sh->ewma_resp_milli = resp_milli;
    } else {
        sh->ewma_rps_milli = (sh->ewma_rps_milli * 7 + rps_milli * 3) / 10;
        sh->ewma_resp_milli = (sh->ewma_resp_milli * 7
                               + resp_milli * 3) / 10;
    }

    capacity = sh->history_capacity;
    if (capacity == 0 || capacity > NGX_HTTP_MONITORING_HISTORY_MAX) {
        capacity = NGX_HTTP_MONITORING_HISTORY_MAX;
    }

    pos = ngx_atomic_fetch_add(&sh->history_write, 1) % capacity;
    sample = &sh->history[pos];
    ngx_memzero(sample, sizeof(ngx_http_monitoring_history_sample_t));

    mem_pct = 0;
    if (sh->system.mem_total > 0) {
        mem_pct = ((double) (sh->system.mem_total
                   - sh->system.mem_available) * 100.0)
                  / (double) sh->system.mem_total;
    }

    swap_pct = 0;
    if (sh->system.swap_total > 0) {
        swap_pct = ((double) (sh->system.swap_total - sh->system.swap_free)
                    * 100.0) / (double) sh->system.swap_total;
    }

    sample->timestamp = tp->sec;
    sample->msec = tp->msec;
    sample->cpu_usage = (double) sh->system.usage_milli / 1000.0;
    sample->load1 = (double) sh->system.load1_milli / 1000.0;
    sample->load5 = (double) sh->system.load5_milli / 1000.0;
    sample->load15 = (double) sh->system.load15_milli / 1000.0;
    sample->memory_used_pct = mem_pct;
    sample->swap_used_pct = swap_pct;
    sample->requests_per_sec = (double) sh->ewma_rps_milli / 1000.0;
    sample->responses_per_sec = (double) sh->ewma_resp_milli / 1000.0;
    sample->network_rx_per_sec = (double) net_rx_rate;
    sample->network_tx_per_sec = (double) net_tx_rate;
    sample->disk_read_per_sec = (double) disk_read_rate;
    sample->disk_write_per_sec = (double) disk_write_rate;
    sample->latency_p95 = ngx_http_monitoring_percentile(sh, 95.0);
    sample->requests_total = req;
    sample->status_4xx = sh->requests.status_4xx;
    sample->status_5xx = sh->requests.status_5xx;

    if (sh->history_count < capacity) {
        ngx_atomic_fetch_add(&sh->history_count, 1);
    }

    sh->prev_req_total = req;
    sh->prev_resp_total = resp;
    sh->prev_net_rx = net_rx;
    sh->prev_net_tx = net_tx;
    sh->prev_disk_read = disk_read;
    sh->prev_disk_write = disk_write;
    sh->prev_collect_msec = now_msec;
    sh->prev_history_msec = now_msec;
}


static ngx_atomic_uint_t
ngx_http_monitoring_sum_network(ngx_http_monitoring_shctx_t *sh, ngx_uint_t tx)
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
ngx_http_monitoring_sum_disk(ngx_http_monitoring_shctx_t *sh, ngx_uint_t write)
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


static ngx_atomic_uint_t
ngx_http_monitoring_delta(ngx_atomic_uint_t now, ngx_atomic_uint_t prev)
{
    return now >= prev ? now - prev : 0;
}


static ngx_uint_t
ngx_http_monitoring_skip_fs(const char *type)
{
    static const char *skip[] = {
        "proc", "sysfs", "devtmpfs", "devpts", "securityfs", "cgroup",
        "cgroup2", "pstore", "autofs", "mqueue", "hugetlbfs", "debugfs",
        "tracefs", "configfs", "fusectl", "binfmt_misc", "rpc_pipefs",
        "overlay", "nsfs", NULL
    };

    ngx_uint_t i;

    for (i = 0; skip[i] != NULL; i++) {
        if (strcmp(type, skip[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

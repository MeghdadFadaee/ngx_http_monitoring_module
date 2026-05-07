ARG DEBIAN_VERSION=bookworm
ARG NGINX_VERSION=1.28.0

FROM debian:${DEBIAN_VERSION}-slim AS builder

ARG NGINX_VERSION
ARG NGINX_CONFIGURE_ARGS="--with-compat --with-http_ssl_module --with-http_stub_status_module --with-http_realip_module --with-http_v2_module --with-http_gzip_static_module"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        libpcre2-dev \
        libssl-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src

RUN curl -fsSLO "https://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz" \
    && tar -xzf "nginx-${NGINX_VERSION}.tar.gz" \
    && rm "nginx-${NGINX_VERSION}.tar.gz"

COPY . /usr/src/ngx_http_monitoring_module

WORKDIR /usr/src/nginx-${NGINX_VERSION}

RUN ./configure \
        --prefix=/usr/local/nginx \
        --sbin-path=/usr/local/sbin/nginx \
        --modules-path=/usr/local/nginx/modules \
        --conf-path=/etc/nginx/nginx.conf \
        --error-log-path=/var/log/nginx/error.log \
        --http-log-path=/var/log/nginx/access.log \
        --pid-path=/var/run/nginx.pid \
        --lock-path=/var/run/nginx.lock \
        --http-client-body-temp-path=/var/cache/nginx/client_temp \
        --http-proxy-temp-path=/var/cache/nginx/proxy_temp \
        --http-fastcgi-temp-path=/var/cache/nginx/fastcgi_temp \
        --http-uwsgi-temp-path=/var/cache/nginx/uwsgi_temp \
        --http-scgi-temp-path=/var/cache/nginx/scgi_temp \
        ${NGINX_CONFIGURE_ARGS} \
        --add-dynamic-module=/usr/src/ngx_http_monitoring_module \
    && make -j"$(nproc)" \
    && make install \
    && mkdir -p /usr/local/nginx/modules \
    && cp objs/ngx_http_monitoring_module.so /usr/local/nginx/modules/


FROM debian:${DEBIAN_VERSION}-slim AS runtime

ARG NGINX_VERSION

LABEL org.opencontainers.image.title="ngx_http_monitoring_module nginx image" \
      org.opencontainers.image.description="Nginx built with ngx_http_monitoring_module and embedded monitoring dashboard" \
      org.opencontainers.image.version="${NGINX_VERSION}"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libpcre2-8-0 \
        libssl3 \
        zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 101 nginx \
    && useradd --system --gid nginx --no-create-home --home /nonexistent \
        --shell /usr/sbin/nologin --uid 101 nginx \
    && mkdir -p /var/cache/nginx/client_temp \
        /var/cache/nginx/proxy_temp \
        /var/cache/nginx/fastcgi_temp \
        /var/cache/nginx/uwsgi_temp \
        /var/cache/nginx/scgi_temp \
        /var/log/nginx \
        /etc/nginx/conf.d \
    && chown -R nginx:nginx /var/cache/nginx /var/log/nginx

COPY --from=builder /usr/local/nginx /usr/local/nginx
COPY --from=builder /usr/local/sbin/nginx /usr/local/sbin/nginx
COPY dockerized/nginx.conf /etc/nginx/nginx.conf

EXPOSE 8080

STOPSIGNAL SIGQUIT

HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD /usr/local/sbin/nginx -t -q && \
        test -r /usr/local/nginx/modules/ngx_http_monitoring_module.so

CMD ["/usr/local/sbin/nginx", "-g", "daemon off;"]

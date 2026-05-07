#!/usr/bin/env sh
set -eu

if [ "${NGINX_SRC:-}" = "" ]; then
    echo "NGINX_SRC is required, for example: NGINX_SRC=/usr/local/src/nginx-1.24.0 ./scripts/build.sh" >&2
    exit 1
fi

NGINX_CONFIGURE_ARGS="${NGINX_CONFIGURE_ARGS:---with-compat --with-http_ssl_module --with-http_stub_status_module}"
BUILD_DIR="${BUILD_DIR:-$(pwd)/build}"

cd "$NGINX_SRC"
./configure $NGINX_CONFIGURE_ARGS --add-dynamic-module="$(cd -P "$(dirname "$0")/.." && pwd)"
make modules

mkdir -p "$BUILD_DIR"
cp "$NGINX_SRC/objs/ngx_http_monitoring_module.so" "$BUILD_DIR/"
echo "Built $BUILD_DIR/ngx_http_monitoring_module.so"

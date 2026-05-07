NGINX_SRC ?=
NGINX_CONFIGURE_ARGS ?= --with-compat --with-http_ssl_module --with-http_stub_status_module
BUILD_DIR ?= $(CURDIR)/build

.PHONY: all module clean install check

all: module

module:
	@test -n "$(NGINX_SRC)" || (echo "Set NGINX_SRC=/path/to/nginx-1.24+ source tree"; exit 1)
	@test -d "$(NGINX_SRC)" || (echo "NGINX_SRC does not exist: $(NGINX_SRC)"; exit 1)
	cd "$(NGINX_SRC)" && ./configure $(NGINX_CONFIGURE_ARGS) --add-dynamic-module="$(CURDIR)"
	$(MAKE) -C "$(NGINX_SRC)" modules
	@mkdir -p "$(BUILD_DIR)"
	@cp "$(NGINX_SRC)/objs/ngx_http_monitoring_module.so" "$(BUILD_DIR)/"
	@echo "Built $(BUILD_DIR)/ngx_http_monitoring_module.so"

install: module
	@test -n "$(DESTDIR)" || (echo "Set DESTDIR to nginx modules directory"; exit 1)
	@mkdir -p "$(DESTDIR)"
	@cp "$(BUILD_DIR)/ngx_http_monitoring_module.so" "$(DESTDIR)/"

check:
	@test -n "$(NGINX_BIN)" || (echo "Set NGINX_BIN=/path/to/nginx"; exit 1)
	"$(NGINX_BIN)" -t -c "$(CURDIR)/examples/nginx.conf"

clean:
	@rm -rf "$(BUILD_DIR)"

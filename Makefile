CC ?= cc
CFLAGS ?= -Wall -Wextra -std=c11
CPPFLAGS ?=
LDFLAGS ?=

PKG_CONFIG ?= pkg-config

CURL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS ?= $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null)

CJSON_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libcjson 2>/dev/null)
CJSON_LIBS ?= $(shell $(PKG_CONFIG) --libs libcjson 2>/dev/null)

MYSQL_CFLAGS :=
MYSQL_LIBS :=

# 1) Prefer pkg-config (mariadb)
MYSQL_CFLAGS := $(shell $(PKG_CONFIG) --cflags mariadb 2>/dev/null)
MYSQL_LIBS := $(shell $(PKG_CONFIG) --libs mariadb 2>/dev/null)

# 2) Fallback to mysql_config from PATH
ifeq ($(strip $(MYSQL_LIBS)),)
MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null)
endif

# 3) Fallback to MacPorts mysql_config path
#    (/opt/local is the default MacPorts prefix)
ifeq ($(strip $(MYSQL_LIBS)),)
MYSQL_CFLAGS := $(shell /opt/local/lib/mariadb/bin/mysql_config --cflags 2>/dev/null)
MYSQL_LIBS := $(shell /opt/local/lib/mariadb/bin/mysql_config --libs 2>/dev/null)
endif

BUILD_DIR := build
BIN_DIR := bin

AIRLY_BIN := $(BIN_DIR)/airly
PMS_BIN := $(BIN_DIR)/pms

AIRLY_OBJS := \
	$(BUILD_DIR)/airly.o \
	$(BUILD_DIR)/airly_http.o \
	$(BUILD_DIR)/airly_json.o \
	$(BUILD_DIR)/airly_db.o

PMS_OBJS := $(BUILD_DIR)/pms.o

.PHONY: all airly pms clean check-config

all: airly pms

airly: $(AIRLY_BIN)

pms: $(PMS_BIN)

$(AIRLY_BIN): check-config $(AIRLY_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(AIRLY_OBJS) $(CURL_LIBS) $(CJSON_LIBS) $(MYSQL_LIBS)

$(PMS_BIN): check-config $(PMS_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(PMS_OBJS) $(MYSQL_LIBS)

check-config:
	@test -f src/config.h || (echo "Missing src/config.h (copy from src/config.h.example)"; exit 1)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CURL_CFLAGS) $(CJSON_CFLAGS) $(MYSQL_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

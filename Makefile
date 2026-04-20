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

# 2) Fallback to pkg-config (libmariadb)
ifeq ($(strip $(MYSQL_LIBS)),)
MYSQL_CFLAGS := $(shell $(PKG_CONFIG) --cflags libmariadb 2>/dev/null)
MYSQL_LIBS := $(shell $(PKG_CONFIG) --libs libmariadb 2>/dev/null)
endif

# 3) Fallback to mysql_config from PATH
ifeq ($(strip $(MYSQL_LIBS)),)
MYSQL_CFLAGS := $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS := $(shell mysql_config --libs 2>/dev/null)
endif

# 4) Fallback to MacPorts mysql_config path
#    (/opt/local is the default MacPorts prefix)
ifeq ($(strip $(MYSQL_LIBS)),)
MYSQL_CFLAGS := $(shell /opt/local/lib/mariadb/bin/mysql_config --cflags 2>/dev/null)
MYSQL_LIBS := $(shell /opt/local/lib/mariadb/bin/mysql_config --libs 2>/dev/null)
endif

# SimpleBLE C bindings detection
# 1) Try pkg-config
SIMPLEBLE_CFLAGS := $(shell $(PKG_CONFIG) --cflags simplecble 2>/dev/null)
SIMPLEBLE_LIBS := $(shell $(PKG_CONFIG) --libs simplecble 2>/dev/null)

# 2) Fallback to relative path compiled sources
ifeq ($(strip $(SIMPLEBLE_LIBS)),)
SIMPLEBLE_CFLAGS := -I../simpleble/simplecble/include -I../simpleble/simplecble/export -Isrc
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SIMPLEBLE_LIBS := -L../simpleble/simplecble/lib -lsimplecble -lsimpleble -lstdc++ -lpthread \
	-framework CoreBluetooth -framework Foundation -framework IOKit -framework IOBluetooth
else
SIMPLEBLE_LIBS := -L../simpleble/simplecble/lib -lsimplecble -lsimpleble -lstdc++ -lpthread -ldbus-1
endif
endif

BUILD_DIR := build
BIN_DIR := bin

AIRLY_BIN := $(BIN_DIR)/airly
PMS_BIN := $(BIN_DIR)/pms
PMS_BT_BIN := $(BIN_DIR)/pms_bt

AIRLY_OBJS := \
	$(BUILD_DIR)/airly.o \
	$(BUILD_DIR)/airly_http.o \
	$(BUILD_DIR)/airly_json.o \
	$(BUILD_DIR)/db.o \
	$(BUILD_DIR)/measurement.o

PMS_OBJS := \
	$(BUILD_DIR)/pms.o \
	$(BUILD_DIR)/pms_packet.o \
	$(BUILD_DIR)/db.o \
	$(BUILD_DIR)/measurement.o

PMS_BT_OBJS := \
	$(BUILD_DIR)/pms_bt.o \
	$(BUILD_DIR)/pms_ble.o \
	$(BUILD_DIR)/pms_packet.o \
	$(BUILD_DIR)/db.o \
	$(BUILD_DIR)/measurement.o

.PHONY: all airly pms pms_bt clean check-config

all: airly pms pms_bt

airly: $(AIRLY_BIN)

pms: $(PMS_BIN)

pms_bt: $(PMS_BT_BIN)

$(AIRLY_BIN): check-config $(AIRLY_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(AIRLY_OBJS) $(CURL_LIBS) $(CJSON_LIBS) $(MYSQL_LIBS)

$(PMS_BIN): check-config $(PMS_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(PMS_OBJS) $(MYSQL_LIBS)

$(PMS_BT_BIN): check-config $(PMS_BT_OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(PMS_BT_OBJS) $(SIMPLEBLE_LIBS) $(MYSQL_LIBS)

check-config:
	@test -f src/config.h || (echo "Missing src/config.h (copy from src/config.h.example)"; exit 1)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR)/pms_bt.o $(BUILD_DIR)/pms_ble.o: $(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MYSQL_CFLAGS) $(SIMPLEBLE_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CURL_CFLAGS) $(CJSON_CFLAGS) $(MYSQL_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

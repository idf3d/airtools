#define _POSIX_C_SOURCE 200809L

#include "pms_ble.h"
#include "config.h"

#include <simplecble/simplecble.h>

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int pipe_fds[2] = {-1, -1};
static simpleble_adapter_t g_adapter = NULL;
static simpleble_peripheral_t g_peripheral = NULL;
static volatile bool g_connected = false;
static volatile bool g_disconnect_seen = false;

#define BLE_NOTIFY_RETRY_COUNT 8
#define BLE_NOTIFY_RETRY_DELAY_US 250000

static void on_notify(simpleble_peripheral_t handle, simpleble_uuid_t service,
                      simpleble_uuid_t characteristic, const uint8_t *data,
                      size_t data_length, void *userdata) {
    (void)handle;
    (void)service;
    (void)characteristic;
    int write_fd = *(int *)userdata;
    if (write_fd >= 0 && data_length > 0) {
        ssize_t n = write(write_fd, data, data_length);
        (void)n;
    }
}

static void on_disconnected(simpleble_peripheral_t handle, void *userdata) {
    (void)handle;
    (void)userdata;
    if (!g_disconnect_seen) {
        printf("BLE device disconnected\n");
        g_disconnect_seen = true;
    }
    g_connected = false;
    if (pipe_fds[1] >= 0) {
        close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }
}

static void cleanup_all(void) {
    if (g_peripheral) {
        simpleble_peripheral_release_handle(g_peripheral);
        g_peripheral = NULL;
    }
    if (g_adapter) {
        simpleble_adapter_release_handle(g_adapter);
        g_adapter = NULL;
    }
    if (pipe_fds[0] >= 0) {
        close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }
    if (pipe_fds[1] >= 0) {
        close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }
    g_connected = false;
    g_disconnect_seen = false;
}

static void ble_sleep_us(long delay_us) {
    struct timespec req;
    struct timespec rem;

    req.tv_sec = delay_us / 1000000L;
    req.tv_nsec = (delay_us % 1000000L) * 1000L;

    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            break;
        }
        req = rem;
    }
}

static bool try_subscribe(simpleble_peripheral_t peripheral,
                          const char *service_uuid, const char *char_uuid,
                          int *write_fd_ptr) {
    simpleble_uuid_t svc_uuid = {0};
    simpleble_uuid_t chr_uuid = {0};

    strncpy(svc_uuid.value, service_uuid, SIMPLEBLE_UUID_STR_LEN - 1);
    strncpy(chr_uuid.value, char_uuid, SIMPLEBLE_UUID_STR_LEN - 1);

    if (simpleble_peripheral_notify(peripheral, svc_uuid, chr_uuid, on_notify,
                                    write_fd_ptr) == SIMPLEBLE_SUCCESS) {
        return true;
    }

    return false;
}

int pms_ble_start(const char *device_name, const char *service_uuid,
                  const char *char_uuid,
                  volatile sig_atomic_t *stop_flag) {
    if (pipe(pipe_fds) != 0) {
        perror("pipe");
        return -1;
    }

    if (!simpleble_adapter_is_bluetooth_enabled()) {
        fprintf(stderr, "Bluetooth is not enabled\n");
        cleanup_all();
        return -1;
    }

    size_t adapter_count = simpleble_adapter_get_count();
    if (adapter_count == 0) {
        fprintf(stderr, "No Bluetooth adapters found\n");
        cleanup_all();
        return -1;
    }

    g_adapter = simpleble_adapter_get_handle(0);
    if (!g_adapter) {
        fprintf(stderr, "Failed to get Bluetooth adapter\n");
        cleanup_all();
        return -1;
    }

    /* Scan repeatedly until device is found, stop is requested, or limit is hit. */
    bool found = false;
    int scan_attempts = 0;
    while (!*stop_flag) {
        scan_attempts++;
        printf("Scanning for BLE device '%s'...\n", device_name);

        if (simpleble_adapter_scan_for(g_adapter, 5000) != SIMPLEBLE_SUCCESS) {
            fprintf(stderr, "BLE scan failed (attempt %d/%d), retrying...\n",
                    scan_attempts, PMS_BT_RECONNECT_MAX_RETRIES);
            if (scan_attempts > PMS_BT_RECONNECT_MAX_RETRIES) {
                fprintf(stderr, "BLE scan retry limit exceeded\n");
                break;
            }
            sleep(1);
            continue;
        }

        size_t results_count = simpleble_adapter_scan_get_results_count(g_adapter);

        for (size_t i = 0; i < results_count; i++) {
            simpleble_peripheral_t p = simpleble_adapter_scan_get_results_handle(g_adapter, i);
            if (!p)
                continue;

            char *name = simpleble_peripheral_identifier(p);
            if (name && strcmp(name, device_name) == 0 && !found) {
                g_peripheral = p;
                found = true;
                simpleble_free(name);
                continue;
            }
            simpleble_free(name);
            simpleble_peripheral_release_handle(p);
        }

        if (found)
            break;

        printf("Device '%s' not found (attempt %d/%d), rescanning...\n",
               device_name, scan_attempts, PMS_BT_RECONNECT_MAX_RETRIES);
        if (scan_attempts > PMS_BT_RECONNECT_MAX_RETRIES) {
            fprintf(stderr, "BLE scan retry limit exceeded\n");
            break;
        }
    }

    if (!found) {
        cleanup_all();
        return -1;
    }

    printf("Found '%s', connecting...\n", device_name);

    if (simpleble_peripheral_connect(g_peripheral) != SIMPLEBLE_SUCCESS) {
        fprintf(stderr, "Failed to connect to '%s'\n", device_name);
        cleanup_all();
        return -1;
    }

    bool connected = false;
    simpleble_peripheral_is_connected(g_peripheral, &connected);
    if (!connected) {
        fprintf(stderr, "Not connected to '%s' after connect call\n", device_name);
        cleanup_all();
        return -1;
    }

    g_connected = true;
    g_disconnect_seen = false;
    printf("Connected to '%s'\n", device_name);

    simpleble_peripheral_set_callback_on_disconnected(g_peripheral,
                                                      on_disconnected, NULL);

    /*
     * Some backends report "connected" before GATT discovery has fully
     * completed. Give the stack a moment so the notification characteristic
     * and its CCCD are available before the first subscribe attempt.
     */
    ble_sleep_us(BLE_NOTIFY_RETRY_DELAY_US);

    /* Pass write fd to notification callback via static pipe_fds */
    static int write_fd_copy;
    write_fd_copy = pipe_fds[1];

    for (int attempt = 1; attempt <= BLE_NOTIFY_RETRY_COUNT && !*stop_flag;
         attempt++) {
        if (try_subscribe(g_peripheral, service_uuid, char_uuid,
                          &write_fd_copy)) {
            printf("Subscribed to notifications on %s/%s\n", service_uuid,
                   char_uuid);
            return pipe_fds[0];
        }

        fprintf(stderr,
                "BLE notify subscribe error on %s/%s (attempt %d/%d)\n",
                service_uuid, char_uuid, attempt,
                BLE_NOTIFY_RETRY_COUNT);

        if (attempt < BLE_NOTIFY_RETRY_COUNT) {
            ble_sleep_us(BLE_NOTIFY_RETRY_DELAY_US);
        }
    }

    simpleble_peripheral_disconnect(g_peripheral);
    cleanup_all();
    return -1;
}

int pms_ble_is_connected(void) {
    return g_connected ? 1 : 0;
}

void pms_ble_stop(void) {
    if (g_peripheral) {
        bool connected = false;
        simpleble_peripheral_is_connected(g_peripheral, &connected);
        if (connected) {
            simpleble_peripheral_disconnect(g_peripheral);
        }
    }
    cleanup_all();
}

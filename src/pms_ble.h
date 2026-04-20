#ifndef PMS_BLE_H
#define PMS_BLE_H

#include <signal.h>

/*
 * Scan for a BLE device, connect, subscribe to notifications.
 * Returns a readable file descriptor (pipe read end) that receives
 * BLE notification data, or -1 on error.
 *
 * Scans repeatedly until the device is found or *stop_flag becomes non-zero.
 * The caller reads from the returned fd just like a UART fd.
 */
int pms_ble_start(const char *device_name, const char *service_uuid,
                  const char *char_uuid,
                  volatile sig_atomic_t *stop_flag);

/* Returns non-zero if the BLE peripheral is currently connected. */
int pms_ble_is_connected(void);

/* Disconnect and release all BLE resources. */
void pms_ble_stop(void);

#endif

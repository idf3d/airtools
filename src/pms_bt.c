#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "db.h"
#include "measurement.h"
#include "pms_ble.h"
#include "pms_packet.h"

static volatile sig_atomic_t stop = 0;
static void handle_sigint(int sig) { (void)sig; stop = 1; }

int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    printf("Starting PMS (Bluetooth), press Ctrl+C to stop...\n");

    while (!stop) {
        int ble_fd = pms_ble_start(PMS_BT_DEVICE_NAME, PMS_BT_SERVICE_UUID,
                                   PMS_BT_CHAR_UUID, &stop);
        if (ble_fd < 0) {
            break;
        }

        uint8_t buf[256];
        int buf_len = 0;

        double sum_values[PMS_MAX_VALUES] = {0};
        int count = 0;
        int extended_count = 0;
        time_t start_time = time(NULL);

        while (!stop) {
            /* read available data from BLE pipe */
            int space = (int)sizeof(buf) - buf_len;
            if (space <= 0) {
                /* buffer full without valid packet, discard oldest byte */
                memmove(buf, buf + 1, (size_t)(buf_len - 1));
                buf_len--;
                continue;
            }

            ssize_t n = read(ble_fd, buf + buf_len, (size_t)space);
            if (n <= 0) {
                if (n == 0) {
                    /* pipe closed = BLE disconnected */
                    printf("BLE connection lost\n");
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                perror("BLE read");
                break;
            }
            buf_len += (int)n;

            /* try to find and parse complete packets in the buffer */
            while (buf_len >= PMS_SHORT_LEN) {
                /* scan for header */
                int hdr_pos = -1;
                int i;
                for (i = 0; i <= buf_len - 2; i++) {
                    if (buf[i] == PMS_HEADER_BYTE1 && buf[i + 1] == PMS_HEADER_BYTE2) {
                        hdr_pos = i;
                        break;
                    }
                }

                if (hdr_pos < 0) {
                    /* no header found, keep last byte (might be start of header) */
                    buf[0] = buf[buf_len - 1];
                    buf_len = 1;
                    break;
                }

                /* discard data before header */
                if (hdr_pos > 0) {
                    memmove(buf, buf + hdr_pos, (size_t)(buf_len - hdr_pos));
                    buf_len -= hdr_pos;
                }

                /* need at least 4 bytes to read the length field */
                if (buf_len < 4) {
                    break;
                }

                uint16_t frame_len = (buf[2] << 8) | buf[3];
                int packet_len;

                if (frame_len == 28) {
                    packet_len = PMS_SHORT_LEN;
                } else if (frame_len == 36) {
                    packet_len = PMS_LONG_LEN;
                } else {
                    /* invalid length, skip this header */
                    memmove(buf, buf + 2, (size_t)(buf_len - 2));
                    buf_len -= 2;
                    continue;
                }

                /* wait for complete packet */
                if (buf_len < packet_len) {
                    break;
                }

                /* parse the packet */
                double values[PMS_MAX_VALUES];
                size_t values_count;

                if (pms_packet_parse(buf, packet_len, &values_count, values) == 0) {
                    size_t k;
                    for (k = 0; k < values_count; k++) {
                        sum_values[k] += values[k];
                    }
                    count++;
                    if (values_count > 3) {
                        extended_count++;
                    }

                    time_t now = time(NULL);

                    if (difftime(now, start_time) >= PMS_BT_AVERAGING_INTERVAL_SEC) {
                        size_t avg_count = extended_count > 0 ? 6 : 3;
                        Measurement *m = measurement_create(start_time, now, avg_count);

                        if (m) {
                            for (k = 0; k < 3; k++) {
                                measurement_set_value(m, k, pms_value_names[k],
                                                      sum_values[k] / count);
                            }
                            if (extended_count > 0) {
                                for (k = 3; k < 6; k++) {
                                    measurement_set_value(m, k, pms_value_names[k],
                                                          sum_values[k] / extended_count);
                                }
                            }

                            const Measurement *mp = m;
                            db_save_measurements(PMS_BT_SENSOR_NAME, &mp, 1);
                            measurement_free(m);
                        }

                        size_t j;
                        for (j = 0; j < PMS_MAX_VALUES; j++) {
                            sum_values[j] = 0;
                        }
                        count = 0;
                        extended_count = 0;
                        start_time = now;
                    }
                }

                /* consume parsed packet from buffer */
                memmove(buf, buf + packet_len, (size_t)(buf_len - packet_len));
                buf_len -= packet_len;
            }
        }

        pms_ble_stop();

        if (!stop) {
            int wait;
            printf("Reconnecting in 5 seconds...\n");
            for (wait = 0; wait < 5 && !stop; wait++) {
                sleep(1);
            }
        }
    }

    printf("\nStopping...\n");
    db_cleanup();
    return 0;
}

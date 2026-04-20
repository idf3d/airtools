#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "config.h"
#include "db.h"
#include "measurement.h"
#include "pms_packet.h"

volatile sig_atomic_t stop = 0;
void handle_sigint(int sig) { (void)sig; stop = 1; }

int uart_fd = -1;

int uart_open(const char *device) {
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("UART open");
        return -1;
    }

    fcntl(fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, PMS_BAUDRATE);
    cfsetospeed(&options, PMS_BAUDRATE);

    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag |= CREAD | CLOCAL;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    options.c_cc[VMIN]  = 1;
    options.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

int main() {
    signal(SIGINT, handle_sigint);

    uart_fd = uart_open(PMS_UART_DEVICE);
    if (uart_fd < 0) return 1;

    printf("Starting PMS7003, press Ctrl+C to stop...\n");

    double sum_values[PMS_MAX_VALUES] = {0};
    int count = 0;
    int extended_count = 0;
    time_t start_time = time(NULL);

    while (!stop) {
        uint8_t b;
        if (read(uart_fd, &b, 1) <= 0) continue;

        if (b == PMS_HEADER_BYTE1) {
            uint8_t b2;
            if (read(uart_fd, &b2, 1) <= 0) continue;

            if (b2 == PMS_HEADER_BYTE2) {
                uint8_t lenbuf[2];
                int read_bytes = 0;

                /* read length field */
                while (read_bytes < 2 && !stop) {
                    int r = read(uart_fd, lenbuf + read_bytes, 2 - read_bytes);
                    if (r > 0) read_bytes += r;
                    else if (r < 0 && errno != EAGAIN) perror("UART read");
                }

                uint16_t frame_len = (lenbuf[0] << 8) | lenbuf[1];
                int packet_len;

                if (frame_len == 28) {
                    packet_len = PMS_SHORT_LEN;
                } else if (frame_len == 36) {
                    packet_len = PMS_LONG_LEN;
                } else {
                    continue;
                }

                /* assemble full packet: header + len + remaining data */
                uint8_t packet[PMS_LONG_LEN];
                packet[0] = PMS_HEADER_BYTE1;
                packet[1] = PMS_HEADER_BYTE2;
                packet[2] = lenbuf[0];
                packet[3] = lenbuf[1];

                int remaining = packet_len - 4;
                read_bytes = 0;

                while (read_bytes < remaining && !stop) {
                    int r = read(uart_fd, packet + 4 + read_bytes, remaining - read_bytes);
                    if (r > 0) read_bytes += r;
                    else if (r < 0 && errno != EAGAIN) perror("UART read");
                }

                double values[PMS_MAX_VALUES];
                size_t values_count;

                if (pms_packet_parse(packet, packet_len, &values_count, values) != 0) {
                    continue;
                }

                size_t k;
                for (k = 0; k < values_count; k++) {
                    sum_values[k] += values[k];
                }
                count++;
                if (values_count > 3) {
                    extended_count++;
                }

                time_t now = time(NULL);

                if (difftime(now, start_time) >= PMS_AVERAGING_INTERVAL_SEC) {
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
                        db_save_measurements(PMS_SENSOR_NAME, &mp, 1);
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
        }
    }

    printf("\nStopping...\n");
    db_cleanup();
    close(uart_fd);
    return 0;
}

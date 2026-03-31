#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "config.h"
#include "pms_db.h"

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
    
    double sum_pm1 = 0, sum_pm25 = 0, sum_pm10 = 0;
    int count = 0;
    time_t start_time = time(NULL);

    while (!stop) {
        uint8_t b;
        if (read(uart_fd, &b, 1) <= 0) continue;

        if (b == 0x42) {
            uint8_t b2;
            if (read(uart_fd, &b2, 1) <= 0) continue;

            if (b2 == 0x4D) {
                uint8_t packet[30];
                int read_bytes = 0;

                while (read_bytes < 30 && !stop) {
                    int r = read(uart_fd, packet + read_bytes, 30 - read_bytes);
                    if (r > 0) read_bytes += r;
                    else if (r < 0 && errno != EAGAIN) perror("UART read");
                }

                uint16_t pm1  = (packet[8] << 8) | packet[9];
                uint16_t pm25 = (packet[10] << 8) | packet[11];
                uint16_t pm10 = (packet[12] << 8) | packet[13];
                sum_pm1  += pm1;
                sum_pm25 += pm25;
                sum_pm10 += pm10;
                count++;
                
                time_t now = time(NULL);
                
                if (difftime(now, start_time) >= PMS_AVERAGING_INTERVAL_SEC) {
                    double avg_pm1  = sum_pm1  / count;
                    double avg_pm25 = sum_pm25 / count;
                    double avg_pm10 = sum_pm10 / count;

                    pms_db_save_average(now, avg_pm1, avg_pm25, avg_pm10);

                    sum_pm1 = sum_pm25 = sum_pm10 = 0;
                    count = 0;
                    start_time = now;
                }
            }
        }
    }

    printf("\nStopping...\n");
    pms_db_cleanup();
    close(uart_fd);
    return 0;
}

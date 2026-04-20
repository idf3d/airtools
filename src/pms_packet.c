#include "pms_packet.h"

#include <stdio.h>

const char *pms_value_names[PMS_MAX_VALUES] = {
    "PM 1.0", "PM 2.5", "PM 10",
    "Formaldehyde", "Temperature", "Humidity"
};

static uint16_t read_u16_be(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static int16_t read_s16_be(const uint8_t *p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

int pms_packet_parse(const uint8_t *buf, int len, size_t *out_count, double *out_values) {
    uint16_t frame_len;
    uint16_t checksum_expected;
    uint16_t checksum_actual = 0;
    int i;

    if (!buf || !out_count || !out_values) {
        return -1;
    }

    if (len != PMS_SHORT_LEN && len != PMS_LONG_LEN) {
        return -1;
    }

    if (buf[0] != PMS_HEADER_BYTE1 || buf[1] != PMS_HEADER_BYTE2) {
        return -1;
    }

    frame_len = read_u16_be(buf + 2);
    if ((len == PMS_SHORT_LEN && frame_len != 28) ||
        (len == PMS_LONG_LEN && frame_len != 36)) {
        return -1;
    }

    /* checksum: sum of all bytes except the last two */
    for (i = 0; i < len - 2; i++) {
        checksum_actual += buf[i];
    }
    checksum_expected = read_u16_be(buf + len - 2);

    if (checksum_actual != checksum_expected) {
        fprintf(stderr, "PMS checksum error: expected %u, got %u\n",
                checksum_expected, checksum_actual);
        return -1;
    }

    /* atmospheric PM values */
    out_values[0] = (double)read_u16_be(buf + 10);  /* PM 1.0 */
    out_values[1] = (double)read_u16_be(buf + 12);  /* PM 2.5 */
    out_values[2] = (double)read_u16_be(buf + 14);  /* PM 10 */

    if (len == PMS_LONG_LEN) {
        out_values[3] = (double)read_u16_be(buf + 28) / 1000.0;  /* Formaldehyde mg/m3 */
        out_values[4] = (double)read_s16_be(buf + 30) / 10.0;    /* Temperature C */
        out_values[5] = (double)read_u16_be(buf + 32) / 10.0;    /* Humidity % */
        *out_count = 6;
    } else {
        *out_count = 3;
    }

    return 0;
}

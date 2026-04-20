#ifndef PMS_PACKET_H
#define PMS_PACKET_H

#include <stddef.h>
#include <stdint.h>

#define PMS_HEADER_BYTE1 0x42
#define PMS_HEADER_BYTE2 0x4D
#define PMS_SHORT_LEN    32
#define PMS_LONG_LEN     40
#define PMS_MAX_VALUES   6

extern const char *pms_value_names[PMS_MAX_VALUES];

int pms_packet_parse(const uint8_t *buf, int len, size_t *out_count, double *out_values);

#endif

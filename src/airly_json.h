#ifndef AIRLY_JSON_H
#define AIRLY_JSON_H

#include "measurement.h"
#include <stddef.h>

int airly_parse_history(const char *json, Measurement ***out_measurements, size_t *out_count);

#endif

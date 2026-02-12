#ifndef AIRLY_JSON_H
#define AIRLY_JSON_H

#include <stddef.h>
#include <time.h>

typedef struct {
    char *name;
    double value;
} ValuePair;

typedef struct {
    time_t from_ts;
    time_t till_ts;
    ValuePair *values;
    size_t values_count;
} AirlyMeasurement;

int airly_parse_history(const char *json, AirlyMeasurement **out_measurements, size_t *out_count);
void airly_free_measurements(AirlyMeasurement *measurements, size_t count);

#endif

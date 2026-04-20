#ifndef MEASUREMENT_H
#define MEASUREMENT_H

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
} Measurement;

Measurement *measurement_create(time_t from_ts, time_t till_ts, size_t values_count);
void measurement_set_value(Measurement *m, size_t index, const char *name, double value);
Measurement *measurement_copy(const Measurement *src);
void measurement_free(Measurement *m);
void measurements_free(Measurement **items, size_t count);

#endif

#define _POSIX_C_SOURCE 200809L

#include "measurement.h"

#include <stdlib.h>
#include <string.h>

Measurement *measurement_create(time_t from_ts, time_t till_ts, size_t values_count) {
    Measurement *m = calloc(1, sizeof(Measurement));
    if (!m) {
        return NULL;
    }

    m->from_ts = from_ts;
    m->till_ts = till_ts;
    m->values_count = values_count;

    if (values_count > 0) {
        m->values = calloc(values_count, sizeof(ValuePair));
        if (!m->values) {
            free(m);
            return NULL;
        }
    }

    return m;
}

void measurement_set_value(Measurement *m, size_t index, const char *name, double value) {
    if (!m || index >= m->values_count) {
        return;
    }

    free(m->values[index].name);
    m->values[index].name = strdup(name);
    m->values[index].value = value;
}

Measurement *measurement_copy(const Measurement *src) {
    Measurement *dst;
    size_t i;

    if (!src) {
        return NULL;
    }

    dst = measurement_create(src->from_ts, src->till_ts, src->values_count);
    if (!dst) {
        return NULL;
    }

    for (i = 0; i < src->values_count; i++) {
        if (src->values[i].name) {
            dst->values[i].name = strdup(src->values[i].name);
            if (!dst->values[i].name) {
                measurement_free(dst);
                return NULL;
            }
        }
        dst->values[i].value = src->values[i].value;
    }

    return dst;
}

void measurement_free(Measurement *m) {
    size_t i;

    if (!m) {
        return;
    }

    for (i = 0; i < m->values_count; i++) {
        free(m->values[i].name);
    }

    free(m->values);
    free(m);
}

void measurements_free(Measurement **items, size_t count) {
    size_t i;

    if (!items) {
        return;
    }

    for (i = 0; i < count; i++) {
        measurement_free(items[i]);
    }

    free(items);
}


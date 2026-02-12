#define _GNU_SOURCE

#include "airly_json.h"

#include <ctype.h>
#if __has_include(<cjson/cJSON.h>)
#include <cjson/cJSON.h>
#elif __has_include(<cJSON.h>)
#include <cJSON.h>
#else
#error "cJSON headers not found (expected cjson/cJSON.h or cJSON.h)"
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_airly_time(const char *s, time_t *out) {
    int year = 0;
    int mon = 0;
    int day = 0;
    int hour = 0;
    int min = 0;
    int sec = 0;
    int tz_h = 0;
    int tz_m = 0;
    int offset = 0;
    int n = 0;
    struct tm tmv;
    time_t base;

    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &mon, &day, &hour, &min, &sec, &n) != 6) {
        return -1;
    }

    const char *tail = s + n;
    if (*tail == '.') {
        tail++;
        while (isdigit((unsigned char)*tail)) {
            tail++;
        }
    }

    if (*tail == 'Z') {
        offset = 0;
        tail++;
    } else if (*tail == '+' || *tail == '-') {
        int sign = (*tail == '+') ? 1 : -1;
        tail++;
        if (!isdigit((unsigned char)tail[0]) || !isdigit((unsigned char)tail[1])) {
            return -1;
        }
        tz_h = (tail[0] - '0') * 10 + (tail[1] - '0');
        tail += 2;
        if (*tail == ':') {
            tail++;
        }
        if (!isdigit((unsigned char)tail[0]) || !isdigit((unsigned char)tail[1])) {
            return -1;
        }
        tz_m = (tail[0] - '0') * 10 + (tail[1] - '0');
        tail += 2;
        offset = sign * (tz_h * 3600 + tz_m * 60);
    } else {
        return -1;
    }

    if (*tail != '\0') {
        return -1;
    }

    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon = mon - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min = min;
    tmv.tm_sec = sec;
    tmv.tm_isdst = -1;

    base = timegm(&tmv);
    if (base == (time_t)-1) {
        return -1;
    }

    *out = base - offset;
    return 0;
}

void airly_free_measurements(AirlyMeasurement *measurements, size_t count) {
    size_t i;
    size_t j;

    if (!measurements) {
        return;
    }

    for (i = 0; i < count; i++) {
        for (j = 0; j < measurements[i].values_count; j++) {
            free(measurements[i].values[j].name);
        }
        free(measurements[i].values);
    }

    free(measurements);
}

int airly_parse_history(const char *json, AirlyMeasurement **out_measurements, size_t *out_count) {
    cJSON *root = NULL;
    cJSON *history = NULL;
    AirlyMeasurement *measurements = NULL;
    int i = 0;
    int count = 0;

    *out_measurements = NULL;
    *out_count = 0;

    root = cJSON_Parse(json);
    if (!root) {
        return -1;
    }

    history = cJSON_GetObjectItemCaseSensitive(root, "history");
    if (!cJSON_IsArray(history)) {
        cJSON_Delete(root);
        return -1;
    }

    count = cJSON_GetArraySize(history);
    if (count <= 0) {
        cJSON_Delete(root);
        return 0;
    }

    measurements = calloc((size_t)count, sizeof(AirlyMeasurement));
    if (!measurements) {
        cJSON_Delete(root);
        return -1;
    }

    for (i = 0; i < count; i++) {
        cJSON *measurement = cJSON_GetArrayItem(history, i);
        cJSON *from = NULL;
        cJSON *till = NULL;
        cJSON *values = NULL;
        int values_count = 0;
        int j = 0;

        if (!cJSON_IsObject(measurement)) {
            airly_free_measurements(measurements, (size_t)count);
            cJSON_Delete(root);
            return -1;
        }

        from = cJSON_GetObjectItemCaseSensitive(measurement, "fromDateTime");
        till = cJSON_GetObjectItemCaseSensitive(measurement, "tillDateTime");
        values = cJSON_GetObjectItemCaseSensitive(measurement, "values");

        if (!cJSON_IsString(from) || from->valuestring == NULL ||
            !cJSON_IsString(till) || till->valuestring == NULL ||
            !cJSON_IsArray(values)) {
            airly_free_measurements(measurements, (size_t)count);
            cJSON_Delete(root);
            return -1;
        }

        if (parse_airly_time(from->valuestring, &measurements[i].from_ts) != 0 ||
            parse_airly_time(till->valuestring, &measurements[i].till_ts) != 0) {
            airly_free_measurements(measurements, (size_t)count);
            cJSON_Delete(root);
            return -1;
        }

        values_count = cJSON_GetArraySize(values);
        if (values_count > 0) {
            measurements[i].values = calloc((size_t)values_count, sizeof(ValuePair));
            if (!measurements[i].values) {
                airly_free_measurements(measurements, (size_t)count);
                cJSON_Delete(root);
                return -1;
            }
        }

        for (j = 0; j < values_count; j++) {
            cJSON *pair = cJSON_GetArrayItem(values, j);
            cJSON *name = NULL;
            cJSON *value = NULL;

            if (!cJSON_IsObject(pair)) {
                airly_free_measurements(measurements, (size_t)count);
                cJSON_Delete(root);
                return -1;
            }

            name = cJSON_GetObjectItemCaseSensitive(pair, "name");
            value = cJSON_GetObjectItemCaseSensitive(pair, "value");

            if (!cJSON_IsString(name) || name->valuestring == NULL || !cJSON_IsNumber(value)) {
                airly_free_measurements(measurements, (size_t)count);
                cJSON_Delete(root);
                return -1;
            }

            measurements[i].values[j].name = strdup(name->valuestring);
            if (!measurements[i].values[j].name) {
                airly_free_measurements(measurements, (size_t)count);
                cJSON_Delete(root);
                return -1;
            }
            measurements[i].values[j].value = value->valuedouble;
            measurements[i].values_count++;
        }
    }

    cJSON_Delete(root);
    *out_measurements = measurements;
    *out_count = (size_t)count;
    return 0;
}

#ifndef AIRLY_DB_H
#define AIRLY_DB_H

#include "airly_json.h"
#include <time.h>

int airly_write_measurements_to_mysql(const AirlyMeasurement *measurements, size_t count);
int airly_get_last_measurement_ts(time_t *out_ts);

#endif

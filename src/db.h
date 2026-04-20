#ifndef DB_H
#define DB_H

#include "measurement.h"

int db_save_measurements(const char *sensor, const Measurement *const *measurements, size_t count);
int db_get_last_ts(const char *sensor, time_t *out_ts);
void db_cleanup(void);

#endif

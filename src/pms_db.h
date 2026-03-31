#ifndef PMS_DB_H
#define PMS_DB_H

#include <time.h>

void pms_db_save_average(time_t sample_time, double pm1, double pm25, double pm10);
void pms_db_cleanup(void);

#endif

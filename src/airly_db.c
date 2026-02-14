#include "airly_db.h"
#include "config.h"

#if __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#elif __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#else
#error "MySQL headers not found (expected mariadb/mysql.h or mysql/mysql.h)"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int airly_write_measurements_to_mysql(const AirlyMeasurement *measurements, size_t count) {
    MYSQL *conn = NULL;
    MYSQL_STMT *stmt = NULL;
    const char *query =
        "INSERT INTO airQuality (ts, sensor, param, `value`) "
        "VALUES (FROM_UNIXTIME(?), ?, ?, ?)";
    size_t i;
    size_t j;

    conn = mysql_init(NULL);
    if (!conn) {
        fprintf(stderr, "mysql_init failed\n");
        return -1;
    }

    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "MySQL connect error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "MySQL stmt init error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    if (mysql_stmt_prepare(stmt, query, (unsigned long)strlen(query)) != 0) {
        fprintf(stderr, "MySQL stmt prepare error: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        mysql_close(conn);
        return -1;
    }

    for (i = 0; i < count; i++) {
        int64_t from_ts = (int64_t)measurements[i].from_ts;
        int64_t till_ts = (int64_t)measurements[i].till_ts;
        long long ts = (long long)(from_ts + (till_ts - from_ts) / 2);
        unsigned long sensor_len = (unsigned long)strlen(AIRLY_SENSOR_LABEL);

        for (j = 0; j < measurements[i].values_count; j++) {
            MYSQL_BIND bind[4];
            double val = measurements[i].values[j].value;
            unsigned long param_len = (unsigned long)strlen(measurements[i].values[j].name);

            memset(bind, 0, sizeof(bind));

            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = &ts;

            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (char *)AIRLY_SENSOR_LABEL;
            bind[1].buffer_length = sensor_len;
            bind[1].length = &sensor_len;

            bind[2].buffer_type = MYSQL_TYPE_STRING;
            bind[2].buffer = measurements[i].values[j].name;
            bind[2].buffer_length = param_len;
            bind[2].length = &param_len;

            bind[3].buffer_type = MYSQL_TYPE_DOUBLE;
            bind[3].buffer = &val;

            if (mysql_stmt_bind_param(stmt, bind) != 0) {
                fprintf(stderr, "MySQL bind error: %s\n", mysql_stmt_error(stmt));
                mysql_stmt_close(stmt);
                mysql_close(conn);
                return -1;
            }

            if (mysql_stmt_execute(stmt) != 0) {
                fprintf(stderr, "MySQL insert error: %s\n", mysql_stmt_error(stmt));
                mysql_stmt_close(stmt);
                mysql_close(conn);
                return -1;
            }
        }
    }

    mysql_stmt_close(stmt);
    mysql_close(conn);
    return 0;
}

int airly_get_last_measurement_ts(time_t *out_ts) {
    MYSQL *conn = NULL;
    MYSQL_RES *result = NULL;
    MYSQL_ROW row;
    const char *query =
        "SELECT UNIX_TIMESTAMP(MAX(ts)) "
        "FROM airQuality "
        "WHERE sensor = '" AIRLY_SENSOR_LABEL "'";

    if (!out_ts) {
        return -1;
    }
    *out_ts = 0;

    conn = mysql_init(NULL);
    if (!conn) {
        fprintf(stderr, "mysql_init failed\n");
        return -1;
    }

    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, "MySQL connect error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    if (mysql_query(conn, query) != 0) {
        fprintf(stderr, "MySQL query error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    result = mysql_store_result(conn);
    if (!result) {
        fprintf(stderr, "MySQL store result error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    row = mysql_fetch_row(result);
    if (row && row[0]) {
        *out_ts = (time_t)strtoll(row[0], NULL, 10);
    }

    mysql_free_result(result);
    mysql_close(conn);
    return 0;
}

#define _POSIX_C_SOURCE 200809L

#include "db.h"
#include "config.h"

#if __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#elif __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#elif __has_include(<mysql.h>)
#include <mysql.h>
#else
#error "MySQL headers not found (expected mariadb/mysql.h, mysql/mysql.h or mysql.h)"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MYSQL *db_connect(void) {
    MYSQL *conn = mysql_init(NULL);
    unsigned int connect_timeout = DB_CONNECT_TIMEOUT_SEC;
    unsigned int read_timeout = DB_READ_TIMEOUT_SEC;
    unsigned int write_timeout = DB_WRITE_TIMEOUT_SEC;

    if (!conn) {
        fprintf(stderr, "mysql_init failed\n");
        return NULL;
    }

    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
    mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
    mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &write_timeout);

    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL,
                            0)) {
        fprintf(stderr, "MySQL connect error: %s\n", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    return conn;
}

typedef struct {
    char *sensor;
    Measurement *measurement;
} PendingEntry;

typedef struct {
    PendingEntry *items;
    size_t count;
    size_t capacity;
} pending_queue_t;

static pending_queue_t pending = {0};

static int enqueue_measurement(const char *sensor, const Measurement *m) {
    PendingEntry entry;

    if (pending.count == pending.capacity) {
        size_t new_cap = pending.capacity == 0 ? 8 : pending.capacity * 2;
        PendingEntry *new_items = realloc(pending.items, new_cap * sizeof(*new_items));
        if (!new_items) {
            perror("realloc");
            return -1;
        }
        pending.items = new_items;
        pending.capacity = new_cap;
    }

    entry.sensor = strdup(sensor);
    if (!entry.sensor) {
        perror("strdup");
        return -1;
    }

    entry.measurement = measurement_copy(m);
    if (!entry.measurement) {
        free(entry.sensor);
        fprintf(stderr, "Failed to copy measurement for queue\n");
        return -1;
    }

    pending.items[pending.count++] = entry;
    return 0;
}

static void free_entry(PendingEntry *entry) {
    free(entry->sensor);
    measurement_free(entry->measurement);
}

static void drop_first_entry(void) {
    if (pending.count == 0) {
        return;
    }

    free_entry(&pending.items[0]);
    if (pending.count > 1) {
        memmove(pending.items, pending.items + 1,
                (pending.count - 1) * sizeof(*pending.items));
    }
    pending.count--;
}

static int write_measurement(MYSQL_STMT *stmt, const char *sensor,
                             const Measurement *m) {
    int64_t from_ts = (int64_t)m->from_ts;
    int64_t till_ts = (int64_t)m->till_ts;
    long long ts = (long long)(from_ts + (till_ts - from_ts) / 2);
    unsigned long sensor_len = (unsigned long)strlen(sensor);
    size_t j;

    for (j = 0; j < m->values_count; j++) {
        MYSQL_BIND bind[4];
        double val = m->values[j].value;
        unsigned long param_len;

        if (!m->values[j].name) {
            continue;
        }

        param_len = (unsigned long)strlen(m->values[j].name);

        memset(bind, 0, sizeof(bind));

        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = &ts;

        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char *)sensor;
        bind[1].buffer_length = sensor_len;
        bind[1].length = &sensor_len;

        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = m->values[j].name;
        bind[2].buffer_length = param_len;
        bind[2].length = &param_len;

        bind[3].buffer_type = MYSQL_TYPE_DOUBLE;
        bind[3].buffer = &val;

        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            fprintf(stderr, "MySQL bind error: %s\n", mysql_stmt_error(stmt));
            return -1;
        }

        if (mysql_stmt_execute(stmt) != 0) {
            fprintf(stderr, "MySQL insert error: %s\n", mysql_stmt_error(stmt));
            return -1;
        }
    }

    return 0;
}

static int flush_pending(MYSQL_STMT *stmt) {
    while (pending.count > 0) {
        if (write_measurement(stmt, pending.items[0].sensor,
                              pending.items[0].measurement) != 0) {
            printf("MySQL replay error, %zu entries still pending\n", pending.count);
            return -1;
        }
        printf("Flushed buffered measurement (%zu remaining)\n", pending.count - 1);
        drop_first_entry();
    }
    return 0;
}

int db_save_measurements(const char *sensor, const Measurement *const *measurements,
                         size_t count) {
    MYSQL *conn = NULL;
    MYSQL_STMT *stmt = NULL;
    const char *query =
        "INSERT INTO airQuality (ts, sensor, param, `value`) "
        "VALUES (FROM_UNIXTIME(?), ?, ?, ?)";
    size_t i;

    conn = db_connect();
    if (!conn) {
        goto enqueue;
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

    if (pending.count > 0 && flush_pending(stmt) != 0) {
        goto enqueue_with_stmt;
    }

    for (i = 0; i < count; i++) {
        if (write_measurement(stmt, sensor, measurements[i]) != 0) {
            /* enqueue remaining */
            for (; i < count; i++) {
                if (enqueue_measurement(sensor, measurements[i]) == 0) {
                    printf("Buffered measurement for retry (%zu pending)\n", pending.count);
                }
            }
            mysql_stmt_close(stmt);
            mysql_close(conn);
            return -1;
        }
    }

    printf("Saved %zu measurement(s) for sensor '%s'\n", count, sensor);
    mysql_stmt_close(stmt);
    mysql_close(conn);
    return 0;

enqueue_with_stmt:
    mysql_stmt_close(stmt);
    mysql_close(conn);

enqueue:
    for (i = 0; i < count; i++) {
        if (enqueue_measurement(sensor, measurements[i]) == 0) {
            printf("Buffered measurement for retry (%zu pending)\n", pending.count);
        } else {
            fprintf(stderr, "Failed to buffer measurement for retry\n");
        }
    }
    return -1;
}

int db_get_last_ts(const char *sensor, time_t *out_ts) {
    MYSQL *conn = NULL;
    MYSQL_STMT *stmt = NULL;
    MYSQL_BIND bind_param[1];
    MYSQL_BIND bind_result[1];
    long long ts_value = 0;
    my_bool is_null = 0;
    unsigned long sensor_len;
    const char *query =
        "SELECT UNIX_TIMESTAMP(MAX(ts)) FROM airQuality WHERE sensor = ?";

    if (!out_ts) {
        return -1;
    }
    *out_ts = 0;

    conn = db_connect();
    if (!conn) {
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

    sensor_len = (unsigned long)strlen(sensor);
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_STRING;
    bind_param[0].buffer = (char *)sensor;
    bind_param[0].buffer_length = sensor_len;
    bind_param[0].length = &sensor_len;

    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        fprintf(stderr, "MySQL bind error: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        mysql_close(conn);
        return -1;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        fprintf(stderr, "MySQL execute error: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        mysql_close(conn);
        return -1;
    }

    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &ts_value;
    bind_result[0].is_null = &is_null;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        fprintf(stderr, "MySQL bind result error: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        mysql_close(conn);
        return -1;
    }

    if (mysql_stmt_fetch(stmt) == 0 && !is_null) {
        *out_ts = (time_t)ts_value;
    }

    mysql_stmt_close(stmt);
    mysql_close(conn);
    return 0;
}

void db_cleanup(void) {
    size_t i;

    for (i = 0; i < pending.count; i++) {
        free_entry(&pending.items[i]);
    }

    free(pending.items);
    pending.items = NULL;
    pending.count = 0;
    pending.capacity = 0;
}


#include "pms_db.h"
#include "config.h"

#if __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#elif __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#else
#error "MySQL headers not found (expected mariadb/mysql.h or mysql/mysql.h)"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} query_queue_t;

static query_queue_t pending_queries = {0};

static char *duplicate_string(const char *value) {
    size_t len;
    char *copy;

    if (!value) {
        return NULL;
    }

    len = strlen(value) + 1;
    copy = malloc(len);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, len);
    return copy;
}

static int enqueue_query(const char *query) {
    if (pending_queries.count == pending_queries.capacity) {
        size_t new_capacity = pending_queries.capacity == 0 ? 8 : pending_queries.capacity * 2;
        char **new_items = realloc(pending_queries.items, new_capacity * sizeof(*new_items));
        if (!new_items) {
            perror("realloc");
            return -1;
        }

        pending_queries.items = new_items;
        pending_queries.capacity = new_capacity;
    }

    pending_queries.items[pending_queries.count] = duplicate_string(query);
    if (!pending_queries.items[pending_queries.count]) {
        perror("malloc");
        return -1;
    }

    pending_queries.count++;
    return 0;
}

static void drop_sent_query(void) {
    if (pending_queries.count == 0) {
        return;
    }

    free(pending_queries.items[0]);
    if (pending_queries.count > 1) {
        memmove(pending_queries.items,
                pending_queries.items + 1,
                (pending_queries.count - 1) * sizeof(*pending_queries.items));
    }
    pending_queries.count--;
}

static int flush_pending_queries(MYSQL *conn) {
    while (pending_queries.count > 0) {
        if (mysql_query(conn, pending_queries.items[0]) != 0) {
            printf("MySQL replay error: %s\n", mysql_error(conn));
            return -1;
        }

        printf("Flushed buffered measurement (%zu remaining)\n", pending_queries.count - 1);
        drop_sent_query();
    }

    return 0;
}

static int format_timestamp(time_t ts, char *buffer, size_t buffer_size) {
    struct tm tm_info;
    struct tm *tmp = localtime(&ts);

    if (!tmp) {
        return -1;
    }

    tm_info = *tmp;

    if (strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &tm_info) == 0) {
        return -1;
    }

    return 0;
}

void pms_db_save_average(time_t sample_time, double pm1, double pm25, double pm10) {
    MYSQL *conn = mysql_init(NULL);
    char query[512];
    char ts[20];

    if (!conn) {
        fprintf(stderr, "mysql_init failed\n");
        return;
    }

    if (format_timestamp(sample_time, ts, sizeof(ts)) != 0) {
        fprintf(stderr, "Failed to format sample timestamp\n");
        mysql_close(conn);
        return;
    }

    snprintf(query, sizeof(query),
             "INSERT INTO airQuality(ts,sensor,param,value) VALUES "
             "('%s','%s','PM 1.0',%.2f),"
             "('%s','%s','PM 2.5',%.2f),"
             "('%s','%s','PM 10',%.2f);",
             ts, PMS_SENSOR_NAME, pm1,
             ts, PMS_SENSOR_NAME, pm25,
             ts, PMS_SENSOR_NAME, pm10);

    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        printf("MySQL connect error: %s\n", mysql_error(conn));
        if (enqueue_query(query) == 0) {
            printf("Buffered measurement for retry (%zu pending)\n", pending_queries.count);
        } else {
            fprintf(stderr, "Failed to buffer measurement for retry\n");
        }
        mysql_close(conn);
        return;
    }

    if (pending_queries.count > 0 && flush_pending_queries(conn) != 0) {
        if (enqueue_query(query) == 0) {
            printf("Buffered current measurement after replay failure (%zu pending)\n",
                   pending_queries.count);
        } else {
            fprintf(stderr, "Failed to buffer current measurement after replay failure\n");
        }
        mysql_close(conn);
        return;
    }

    if (mysql_query(conn, query) != 0) {
        printf("MySQL insert error: %s\n", mysql_error(conn));
        if (enqueue_query(query) == 0) {
            printf("Buffered measurement for retry (%zu pending)\n", pending_queries.count);
        } else {
            fprintf(stderr, "Failed to buffer measurement for retry\n");
        }
    } else {
        printf("Saved hourly average at %s: pm1=%.2f pm2.5=%.2f pm10=%.2f\n",
               ts, pm1, pm25, pm10);
    }

    mysql_close(conn);
}

void pms_db_cleanup(void) {
    size_t i;

    for (i = 0; i < pending_queries.count; i++) {
        free(pending_queries.items[i]);
    }

    free(pending_queries.items);
    pending_queries.items = NULL;
    pending_queries.count = 0;
    pending_queries.capacity = 0;
}

#include "airly_db.h"
#include "airly_http.h"
#include "airly_json.h"

#include <curl/curl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define LOOP_INTERVAL_SEC (60 * 60)
#define FETCH_INTERVAL_SEC (24 * 60 * 60)

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig) {
    (void)sig;
    stop_requested = 1;
}

static void sleep_with_stop(unsigned int sec) {
    while (sec > 0 && !stop_requested) {
        sec = sleep(sec);
    }
}

static void run_once(void) {
    char *json = NULL;
    AirlyMeasurement *measurements = NULL;
    size_t count = 0;
    time_t last_ts = 0;
    time_t now = 0;

    if (airly_get_last_measurement_ts(&last_ts) != 0) {
        return;
    }

    now = time(NULL);
    if (last_ts != 0 && difftime(now, last_ts) < FETCH_INTERVAL_SEC) {
        printf("Skip fetch: last reading is newer than 24h\n");
        return;
    }

    if (airly_fetch_json(&json) != 0) {
        return;
    }

    if (airly_parse_history(json, &measurements, &count) != 0) {
        fprintf(stderr, "JSON parse error (history)\n");
        free(json);
        return;
    }

    if (airly_write_measurements_to_mysql(measurements, count) == 0) {
        printf("Saved %zu history measurements to MySQL\n", count);
    }

    airly_free_measurements(measurements, count);
    free(json);
}

int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "curl_global_init failed\n");
        return 1;
    }

    while (!stop_requested) {
        run_once();
        if (!stop_requested) {
            sleep_with_stop(LOOP_INTERVAL_SEC);
        }
    }

    curl_global_cleanup();
    return 0;
}

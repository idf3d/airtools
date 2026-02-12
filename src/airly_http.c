#include "airly_http.h"
#include "config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t size;
} Buffer;

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    Buffer *buf = (Buffer *)userp;

    char *tmp = realloc(buf->data, buf->size + realsize + 1);
    if (!tmp) {
        return 0;
    }

    buf->data = tmp;
    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    return realsize;
}

int airly_fetch_json(char **out_json) {
    CURL *curl = NULL;
    CURLcode res;
    long http_code = 0;
    char url[512];
    Buffer response = {0};
    struct curl_slist *headers = NULL;

    snprintf(url, sizeof(url), "%s?lat=%s&lng=%s", AIRLY_API_URL, AIRLY_LAT, AIRLY_LNG);

    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "apikey: " AIRLY_API_KEY);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code > 299) {
        fprintf(stderr, "Airly HTTP error: %ld\n", http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(response.data);
        return -1;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    *out_json = response.data;
    return 0;
}

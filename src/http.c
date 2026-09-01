#include "http.h"
#include "version.h"

#include <curl/curl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Growable buffer used to accumulate the response body as curl delivers
 * it in chunks. */
struct write_ctx {
    char *data;
    size_t size;
    size_t cap;
};

static void set_err(char *errbuf, size_t errbuf_len, const char *fmt, ...) {
    if (!errbuf || errbuf_len == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, errbuf_len, fmt, ap);
    va_end(ap);
}

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct write_ctx *ctx = (struct write_ctx *)userp;

    if (ctx->size + realsize + 1 > ctx->cap) {
        size_t newcap = ctx->cap == 0 ? 4096 : ctx->cap;
        while (newcap < ctx->size + realsize + 1) {
            newcap *= 2;
        }
        char *newdata = realloc(ctx->data, newcap);
        if (!newdata) {
            return 0; /* signals an error back to curl */
        }
        ctx->data = newdata;
        ctx->cap = newcap;
    }

    memcpy(ctx->data + ctx->size, contents, realsize);
    ctx->size += realsize;
    ctx->data[ctx->size] = '\0';
    return realsize;
}

/* Enforces the Repology API rate limit of at most one request per second.
 * This tool is single-threaded, so a function-local static is sufficient
 * to track the timing of the previous request across calls. */
static void throttle_before_request(void) {
    static struct timespec last_request;
    static int have_last = 0;
    static const long min_interval_ns = 1000000000L; /* 1 second */

    if (have_last) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        long elapsed_ns = (long)(now.tv_sec - last_request.tv_sec) * 1000000000L +
                           (now.tv_nsec - last_request.tv_nsec);

        if (elapsed_ns >= 0 && elapsed_ns < min_interval_ns) {
            long remaining_ns = min_interval_ns - elapsed_ns;
            struct timespec sleep_ts;
            sleep_ts.tv_sec = remaining_ns / 1000000000L;
            sleep_ts.tv_nsec = remaining_ns % 1000000000L;
            nanosleep(&sleep_ts, NULL);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &last_request);
    have_last = 1;
}

int http_global_init(void) {
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void http_global_cleanup(void) {
    curl_global_cleanup();
}

int http_get(const char *url, http_response_t *out, char *errbuf, size_t errbuf_len) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!url || !out) {
        set_err(errbuf, errbuf_len, "internal error: invalid arguments to http_get");
        return -1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        set_err(errbuf, errbuf_len, "failed to initialize curl handle");
        return -1;
    }

    /* Rate-limit ourselves before every request, including the first
     * page of a paginated search, so we never exceed 1 request/second. */
    throttle_before_request();

    struct write_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    char ua[256];
    snprintf(ua, sizeof(ua), "repoq/%s (+%s)", REPOQ_VERSION, REPOQ_REPO_URL);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");

    char curl_errbuf[CURL_ERROR_SIZE];
    curl_errbuf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        if (curl_errbuf[0] != '\0') {
            set_err(errbuf, errbuf_len, "HTTP request failed: %s", curl_errbuf);
        } else {
            set_err(errbuf, errbuf_len, "HTTP request failed: %s", curl_easy_strerror(res));
        }
        free(ctx.data);
        curl_easy_cleanup(curl);
        return -1;
    }

    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_easy_cleanup(curl);

    if (ctx.data == NULL) {
        /* Empty body: still hand back a valid, non-NULL, NUL-terminated
         * string so callers can treat out->data uniformly. */
        ctx.data = malloc(1);
        if (!ctx.data) {
            set_err(errbuf, errbuf_len, "out of memory");
            return -1;
        }
        ctx.data[0] = '\0';
        ctx.size = 0;
    }

    out->data = ctx.data;
    out->size = ctx.size;
    out->status_code = status_code;
    return 0;
}

void http_response_free(http_response_t *resp) {
    if (!resp) {
        return;
    }
    free(resp->data);
    resp->data = NULL;
    resp->size = 0;
    resp->status_code = 0;
}

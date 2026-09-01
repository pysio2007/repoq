#ifndef REPOQ_HTTP_H
#define REPOQ_HTTP_H

#include <stddef.h>

/* Holds the raw result of an HTTP GET request. */
typedef struct {
    char *data;       /* NUL-terminated response body, owned by this struct */
    size_t size;      /* length of data, excluding the terminating NUL */
    long status_code; /* HTTP status code, e.g. 200, 404 */
} http_response_t;

/* Must be called once before any http_get() call, and matched with a call
 * to http_global_cleanup() before the program exits.
 * Returns 0 on success, -1 on failure. */
int http_global_init(void);
void http_global_cleanup(void);

/*
 * Performs a rate-limited HTTP GET request against `url`.
 *
 * This function enforces the Repology API rate limit (no more than one
 * request per second) by sleeping as needed before issuing the request.
 * It also attaches a compliant User-Agent header identifying this tool.
 *
 * On success, returns 0 and fills `*out` with the response body and HTTP
 * status code. The caller must release it with http_response_free().
 * Note that a successful return only means the transfer completed; the
 * caller should still inspect out->status_code (e.g. for 404 handling).
 *
 * On failure (network error, timeout, could not initialize curl, etc.),
 * returns -1 and writes a human-readable message into `errbuf` (if
 * `errbuf` is non-NULL and `errbuf_len` > 0). `*out` is left zeroed.
 */
int http_get(const char *url, http_response_t *out, char *errbuf, size_t errbuf_len);

/* Frees resources owned by an http_response_t previously filled by
 * http_get(). Safe to call on a zeroed or already-freed struct. */
void http_response_free(http_response_t *resp);

/* Percent-encodes a string for safe use as a single URL path segment or
 * query parameter value, per RFC 3986 (unreserved characters are passed
 * through unchanged, everything else becomes %XX). Returns a newly
 * allocated string (caller must free()), or NULL on allocation failure. */
char *url_encode(const char *s);

#endif /* REPOQ_HTTP_H */

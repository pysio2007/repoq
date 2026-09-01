#ifndef REPOQ_JSON_PARSE_H
#define REPOQ_JSON_PARSE_H

#include <stddef.h>

/* A single package record, as returned by the Repology API. All string
 * fields are heap-allocated (owned by this struct) and may be NULL if the
 * corresponding field was absent from the JSON response, since only
 * "repo" and "version" are mandatory per the API docs. */
typedef struct {
    char *repo;
    char *subrepo;
    char *srcname;
    char *binname;
    char *visiblename;
    char *version;
    char *origversion;
    char *status;
    char *summary;
} package_t;

typedef struct {
    package_t *items;
    size_t count;
} package_list_t;

/* A project groups together packages from possibly-different repositories
 * that Repology considers to be "the same" piece of software. */
typedef struct {
    char *name;
    package_list_t packages;
} named_project_t;

typedef struct {
    named_project_t *items;
    size_t count;
} project_list_t;

/* Parses the response body of GET /api/v1/project/<name>/, which is a
 * JSON array of package objects.
 *
 * On success, returns 0 and fills *out, which the caller must eventually
 * release with package_list_free(). On malformed JSON, returns -1 and
 * writes a human-readable message into errbuf (if non-NULL). */
int json_parse_package_list(const char *json_text, package_list_t *out, char *errbuf, size_t errbuf_len);

/* Parses the response body of GET /api/v1/projects/..., which is a JSON
 * object mapping project name -> array of package objects.
 *
 * On success, returns 0 and fills *out, which the caller must eventually
 * release with project_list_free(). On malformed JSON, returns -1 and
 * writes a human-readable message into errbuf (if non-NULL). */
int json_parse_project_list(const char *json_text, project_list_t *out, char *errbuf, size_t errbuf_len);

void package_list_free(package_list_t *list);
void project_list_free(project_list_t *list);

/* Collapses packages that share the same (repo, version, origversion,
 * status) tuple down to a single entry, keeping the first one
 * encountered. This is useful when a repo ships many sub-packages from
 * the same source (e.g. "frr-bgpd", "frr-ospfd", "frr-zebra", ...) that
 * would otherwise all render as seemingly-duplicate rows. */
void package_list_dedupe(package_list_t *list);

/* Applies package_list_dedupe() to every project's package list. */
void project_list_dedupe(project_list_t *list);

#endif /* REPOQ_JSON_PARSE_H */

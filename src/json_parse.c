#include "json_parse.h"

#include "cJSON.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *errbuf, size_t errbuf_len, const char *fmt, ...) {
    if (!errbuf || errbuf_len == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, errbuf_len, fmt, ap);
    va_end(ap);
}

static char *dup_string_field(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item) || item->valuestring == NULL) {
        return NULL;
    }
    return strdup(item->valuestring);
}

static void package_free_fields(package_t *pkg) {
    free(pkg->repo);
    free(pkg->subrepo);
    free(pkg->srcname);
    free(pkg->binname);
    free(pkg->visiblename);
    free(pkg->version);
    free(pkg->origversion);
    free(pkg->status);
    free(pkg->summary);
    memset(pkg, 0, sizeof(*pkg));
}

static void parse_package_object(const cJSON *obj, package_t *pkg) {
    memset(pkg, 0, sizeof(*pkg));
    if (!cJSON_IsObject(obj)) {
        return;
    }
    pkg->repo = dup_string_field(obj, "repo");
    pkg->subrepo = dup_string_field(obj, "subrepo");
    pkg->srcname = dup_string_field(obj, "srcname");
    pkg->binname = dup_string_field(obj, "binname");
    pkg->visiblename = dup_string_field(obj, "visiblename");
    pkg->version = dup_string_field(obj, "version");
    pkg->origversion = dup_string_field(obj, "origversion");
    pkg->status = dup_string_field(obj, "status");
    pkg->summary = dup_string_field(obj, "summary");
}

/* Parses a JSON array of package objects (array_node must satisfy
 * cJSON_IsArray()) into a freshly populated package_list_t. Used both for
 * the top-level single-project response and for each project's package
 * array inside the multi-project response, so the two can't drift apart.
 * Returns 0 on success (including an empty array), -1 only on allocation
 * failure, in which case *out is left zeroed. */
static int parse_package_array(const cJSON *array_node, package_list_t *out) {
    memset(out, 0, sizeof(*out));

    size_t count = (size_t)cJSON_GetArraySize(array_node);
    if (count == 0) {
        return 0;
    }

    package_t *items = calloc(count, sizeof(package_t));
    if (!items) {
        return -1;
    }

    size_t i = 0;
    const cJSON *elem = NULL;
    cJSON_ArrayForEach(elem, array_node) {
        parse_package_object(elem, &items[i]);
        i++;
    }

    out->items = items;
    out->count = count;
    return 0;
}

void package_list_free(package_list_t *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        package_free_fields(&list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

void project_list_free(project_list_t *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].name);
        package_list_free(&list->items[i].packages);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static bool str_eq_nullable(const char *a, const char *b) {
    if (a == NULL && b == NULL) {
        return true;
    }
    if (a == NULL || b == NULL) {
        return false;
    }
    return strcmp(a, b) == 0;
}

void package_list_dedupe(package_list_t *list) {
    if (!list || list->count == 0) {
        return;
    }

    size_t kept = 0;
    for (size_t i = 0; i < list->count; i++) {
        bool is_dup = false;
        for (size_t j = 0; j < kept; j++) {
            if (str_eq_nullable(list->items[i].repo, list->items[j].repo) &&
                str_eq_nullable(list->items[i].version, list->items[j].version) &&
                str_eq_nullable(list->items[i].origversion, list->items[j].origversion) &&
                str_eq_nullable(list->items[i].status, list->items[j].status)) {
                is_dup = true;
                break;
            }
        }
        if (is_dup) {
            package_free_fields(&list->items[i]);
        } else {
            if (kept != i) {
                list->items[kept] = list->items[i];
            }
            kept++;
        }
    }
    list->count = kept;
}

void project_list_dedupe(project_list_t *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        package_list_dedupe(&list->items[i].packages);
    }
}

int json_parse_package_list(const char *json_text, package_list_t *out, char *errbuf, size_t errbuf_len) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!json_text || !out) {
        set_err(errbuf, errbuf_len, "internal error: invalid arguments to json_parse_package_list");
        return -1;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        set_err(errbuf, errbuf_len, "failed to parse JSON response from server");
        return -1;
    }
    if (!cJSON_IsArray(root)) {
        set_err(errbuf, errbuf_len, "unexpected JSON response format (expected an array)");
        cJSON_Delete(root);
        return -1;
    }

    if (parse_package_array(root, out) != 0) {
        set_err(errbuf, errbuf_len, "out of memory");
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

int json_parse_project_list(const char *json_text, project_list_t *out, char *errbuf, size_t errbuf_len) {
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!json_text || !out) {
        set_err(errbuf, errbuf_len, "internal error: invalid arguments to json_parse_project_list");
        return -1;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        set_err(errbuf, errbuf_len, "failed to parse JSON response from server");
        return -1;
    }
    if (!cJSON_IsObject(root)) {
        set_err(errbuf, errbuf_len, "unexpected JSON response format (expected an object)");
        cJSON_Delete(root);
        return -1;
    }

    size_t count = 0;
    {
        const cJSON *e = NULL;
        cJSON_ArrayForEach(e, root) {
            count++;
        }
    }

    named_project_t *items = NULL;
    if (count > 0) {
        items = calloc(count, sizeof(named_project_t));
        if (!items) {
            set_err(errbuf, errbuf_len, "out of memory");
            cJSON_Delete(root);
            return -1;
        }
    }

    size_t i = 0;
    const cJSON *proj_entry = NULL;
    cJSON_ArrayForEach(proj_entry, root) {
        named_project_t *np = &items[i];
        memset(np, 0, sizeof(*np));
        np->name = proj_entry->string ? strdup(proj_entry->string) : NULL;

        if (cJSON_IsArray(proj_entry)) {
            /* On allocation failure we simply leave this project's
             * package list empty rather than aborting the whole parse;
             * parse_package_array() already leaves it zeroed. */
            parse_package_array(proj_entry, &np->packages);
        }
        i++;
    }

    cJSON_Delete(root);

    out->items = items;
    out->count = count;
    return 0;
}

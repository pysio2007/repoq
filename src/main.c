#include "http.h"
#include "json_parse.h"
#include "output.h"
#include "version.h"

#include "cJSON.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SEARCH_LIMIT 200
#define API_PAGE_SIZE 200
#define API_BASE_URL "https://repology.org/api/v1"

static void print_usage(FILE *out) {
    fprintf(out,
            "Usage: repoq <project-name> [options]\n"
            "       repoq --search <substring> [options]\n"
            "       repoq --outdated [--search <substring>] [options]\n");
}

static void print_help(void) {
    print_usage(stdout);
    printf("\n"
           "Query the Repology API (https://repology.org) from the command line.\n"
           "\n"
           "Modes:\n"
           "  repoq <project-name>       Show packages for a single Repology project.\n"
           "  repoq --search <substring> Search for projects whose name contains <substring>.\n"
           "  repoq --outdated           List outdated projects (can be combined with --search).\n"
           "\n"
           "Options:\n"
           "  -r, --repo <name>      Only show packages from repository <name>.\n"
           "                          (only valid together with a single project name)\n"
           "  -s, --search <substr>  Search projects by substring (batch mode).\n"
           "  -o, --outdated          Only show outdated/legacy projects (batch mode).\n"
           "  -l, --limit <N>         Limit the number of projects shown in batch mode\n"
           "                          (default: %d, which is one API page).\n"
           "  -j, --json              Print the raw JSON response instead of a table.\n"
           "  -n, --no-color          Disable colored output.\n"
           "  -u, --unique            Collapse rows that share the same repo/version/status\n"
           "                          (hides sibling sub-packages, e.g. \"foo-bgpd\" vs\n"
           "                          \"foo-ospfd\" from the same source package).\n"
           "  -h, --help              Show this help message and exit.\n"
           "  -v, --version           Show version information and exit.\n"
           "\n"
           "Examples:\n"
           "  repoq firefox\n"
           "  repoq firefox --repo aosc\n"
           "  repoq --search fire --limit 20\n"
           "  repoq --search fire --outdated\n"
           "  repoq firefox --json | jq .\n"
           "\n"
           "Notes:\n"
           "  A single repo can ship many sibling packages built from the same source\n"
           "  (e.g. \"frr\", \"frr-bgpd\", \"frr-ospfd\", ...), which show up as separate\n"
           "  rows with the same version/status; the PACKAGE column identifies which\n"
           "  is which, and --unique can collapse them if you don't care.\n"
           "\n"
           "  repoq respects Repology's API terms of use: requests are throttled to at\n"
           "  most one per second, and a custom User-Agent identifying this tool and its\n"
           "  source repository is sent with every request.\n",
           DEFAULT_SEARCH_LIMIT);
}

static void print_version(void) {
    printf("repoq %s (%s)\n", REPOQ_VERSION, REPOQ_REPO_URL);
}

/* Percent-encodes a string for safe use as a single path segment or query
 * parameter value, per RFC 3986 (unreserved characters are passed through
 * unchanged, everything else becomes %XX). Returns a newly allocated
 * string, or NULL on allocation failure. */
static char *url_encode(const char *s) {
    if (!s) {
        return strdup("");
    }
    size_t len = strlen(s);
    char *out = malloc(len * 3 + 1);
    if (!out) {
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            snprintf(out + o, 4, "%%%02X", c);
            o += 3;
        }
    }
    out[o] = '\0';
    return out;
}

static char *reserialize_package_list(const package_list_t *list);

static int cmd_project(const char *project_name, const char *repo_filter, bool json_mode, bool use_color, bool unique_flag) {
    char *encoded = url_encode(project_name);
    if (!encoded) {
        fprintf(stderr, "repoq: out of memory\n");
        return 1;
    }

    char url[2048];
    snprintf(url, sizeof(url), "%s/project/%s", API_BASE_URL, encoded);
    free(encoded);

    http_response_t resp;
    char errbuf[256];
    if (http_get(url, &resp, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "repoq: %s\n", errbuf);
        return 1;
    }

    int exit_code = 0;

    if (resp.status_code == 404) {
        fprintf(stderr, "repoq: project '%s' was not found on Repology\n", project_name);
        exit_code = 1;
    } else if (resp.status_code != 200) {
        fprintf(stderr, "repoq: server returned HTTP %ld for project '%s'\n", resp.status_code, project_name);
        exit_code = 1;
    } else if (json_mode && !unique_flag) {
        /* Verbatim passthrough: --unique needs to transform the parsed
         * data first, so it can't reuse the server's raw response bytes
         * (handled in the branch below instead). */
        output_print_raw_json(resp.data);
    } else {
        package_list_t list;
        char perr[256];
        if (json_parse_package_list(resp.data, &list, perr, sizeof(perr)) != 0) {
            fprintf(stderr, "repoq: failed to parse server response: %s\n", perr);
            exit_code = 1;
        } else if (list.count == 0) {
            /* Repology returns HTTP 200 with an empty array for project
             * names it doesn't recognize, rather than a 404. */
            fprintf(stderr, "repoq: project '%s' was not found on Repology\n", project_name);
            exit_code = 1;
            package_list_free(&list);
        } else {
            if (unique_flag) {
                package_list_dedupe(&list);
            }
            if (json_mode) {
                char *text = reserialize_package_list(&list);
                if (text) {
                    output_print_raw_json(text);
                    free(text);
                } else {
                    fprintf(stderr, "repoq: out of memory while building JSON output\n");
                    exit_code = 1;
                }
            } else {
                output_print_package_table(&list, repo_filter, use_color);
            }
            package_list_free(&list);
        }
    }

    http_response_free(&resp);
    return exit_code;
}

static cJSON *package_to_json(const package_t *p) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return NULL;
    }
    if (p->repo) cJSON_AddStringToObject(obj, "repo", p->repo);
    if (p->subrepo) cJSON_AddStringToObject(obj, "subrepo", p->subrepo);
    if (p->srcname) cJSON_AddStringToObject(obj, "srcname", p->srcname);
    if (p->binname) cJSON_AddStringToObject(obj, "binname", p->binname);
    if (p->visiblename) cJSON_AddStringToObject(obj, "visiblename", p->visiblename);
    if (p->version) cJSON_AddStringToObject(obj, "version", p->version);
    if (p->origversion) cJSON_AddStringToObject(obj, "origversion", p->origversion);
    if (p->status) cJSON_AddStringToObject(obj, "status", p->status);
    if (p->summary) cJSON_AddStringToObject(obj, "summary", p->summary);
    return obj;
}

/* Serializes a package list back into a JSON array, matching the shape
 * of GET /api/v1/project/<name>. Used for --json output when the list
 * has been transformed locally (e.g. by --unique) and can no longer be
 * passed through verbatim from the server's response bytes. */
static char *reserialize_package_list(const package_list_t *list) {
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return NULL;
    }
    for (size_t i = 0; i < list->count; i++) {
        cJSON *obj = package_to_json(&list->items[i]);
        if (obj) {
            cJSON_AddItemToArray(root, obj);
        }
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

/* Serializes a (possibly multi-page, possibly limit-truncated) collection
 * of projects back into a single JSON object of the same shape the API
 * itself returns ({project_name: [package, ...], ...}), for --json output
 * when a verbatim single-page passthrough is not possible. */
static char *reserialize_project_list(const project_list_t *list) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    for (size_t i = 0; i < list->count; i++) {
        const named_project_t *np = &list->items[i];
        cJSON *pkgs = cJSON_CreateArray();
        if (!pkgs) {
            continue;
        }
        for (size_t j = 0; j < np->packages.count; j++) {
            cJSON *obj = package_to_json(&np->packages.items[j]);
            if (obj) {
                cJSON_AddItemToArray(pkgs, obj);
            }
        }
        cJSON_AddItemToObject(root, np->name ? np->name : "", pkgs);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

/* Linear scan is fine here: result sets are bounded by --limit, which
 * is expected to stay in the hundreds to low thousands. */
static bool project_list_contains(const project_list_t *list, const char *name) {
    if (!name) {
        return false;
    }
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].name && strcmp(list->items[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static int cmd_search(const char *search_term, bool outdated_only, long limit_arg, bool json_mode, bool use_color, bool unique_flag) {
    size_t limit = (limit_arg > 0) ? (size_t)limit_arg : DEFAULT_SEARCH_LIMIT;

    project_list_t collected;
    memset(&collected, 0, sizeof(collected));
    size_t cap = 0;

    char *cursor = NULL;
    char *first_page_raw = NULL;
    int pages_fetched = 0;
    bool truncated = false;
    int exit_code = 0;

    while (collected.count < limit) {
        char *enc_search = (search_term && search_term[0]) ? url_encode(search_term) : NULL;
        char *enc_cursor = cursor ? url_encode(cursor) : NULL;

        char url[2048];
        if (enc_cursor) {
            snprintf(url, sizeof(url), "%s/projects/%s/?", API_BASE_URL, enc_cursor);
        } else {
            snprintf(url, sizeof(url), "%s/projects/?", API_BASE_URL);
        }
        free(enc_cursor);

        size_t url_len = strlen(url);
        if (enc_search) {
            snprintf(url + url_len, sizeof(url) - url_len, "search=%s&", enc_search);
            url_len = strlen(url);
            free(enc_search);
        }
        if (outdated_only) {
            snprintf(url + url_len, sizeof(url) - url_len, "outdated=1&");
        }

        http_response_t resp;
        char errbuf[256];
        if (http_get(url, &resp, errbuf, sizeof(errbuf)) != 0) {
            fprintf(stderr, "repoq: %s\n", errbuf);
            exit_code = 1;
            break;
        }

        if (resp.status_code != 200) {
            fprintf(stderr, "repoq: server returned HTTP %ld while searching\n", resp.status_code);
            http_response_free(&resp);
            exit_code = 1;
            break;
        }

        project_list_t page;
        char perr[256];
        if (json_parse_project_list(resp.data, &page, perr, sizeof(perr)) != 0) {
            fprintf(stderr, "repoq: failed to parse server response: %s\n", perr);
            http_response_free(&resp);
            exit_code = 1;
            break;
        }

        pages_fetched++;
        if (json_mode) {
            if (pages_fetched == 1) {
                first_page_raw = strdup(resp.data);
            } else if (first_page_raw) {
                free(first_page_raw);
                first_page_raw = NULL;
            }
        }

        http_response_free(&resp);

        /* IMPORTANT: the API does not return object keys in any
         * guaranteed (let alone alphabetical) order within a page -- two
         * identical requests can come back with a different key order,
         * and this has been observed empirically. What IS guaranteed is
         * that the *set* of returned project names falls within the
         * alphabetical range starting at `cursor` (inclusive). So the
         * cursor for the next page must be the lexicographically
         * largest name seen in this page, not "whatever came last while
         * iterating", and de-duplication against already-collected
         * projects must check every incoming name rather than assuming
         * only the first item of a page can repeat the previous cursor. */
        size_t page_count = page.count;
        bool full_page = (page_count == API_PAGE_SIZE);

        char *next_cursor = NULL;
        for (size_t i = 0; i < page_count; i++) {
            const char *name = page.items[i].name;
            if (name && (!next_cursor || strcmp(name, next_cursor) > 0)) {
                free(next_cursor);
                next_cursor = strdup(name);
            }
        }

        for (size_t i = 0; i < page_count; i++) {
            if (collected.count >= limit) {
                truncated = true;
                break;
            }
            if (page.items[i].name && project_list_contains(&collected, page.items[i].name)) {
                continue; /* boundary item already collected from a previous page */
            }
            if (collected.count + 1 > cap) {
                size_t newcap = cap == 0 ? 16 : cap * 2;
                named_project_t *items = realloc(collected.items, newcap * sizeof(named_project_t));
                if (!items) {
                    fprintf(stderr, "repoq: out of memory\n");
                    exit_code = 1;
                    break;
                }
                collected.items = items;
                cap = newcap;
            }
            /* Move ownership of this project's data into collected;
             * zero out the source slot so project_list_free() below
             * treats it as empty rather than double-freeing. */
            collected.items[collected.count] = page.items[i];
            memset(&page.items[i], 0, sizeof(named_project_t));
            collected.count++;
        }

        project_list_free(&page);

        if (exit_code != 0) {
            free(next_cursor);
            break;
        }
        if (collected.count >= limit || !full_page || !next_cursor) {
            free(next_cursor);
            break;
        }

        free(cursor);
        cursor = next_cursor;
    }

    free(cursor);

    if (exit_code == 0 && unique_flag) {
        project_list_dedupe(&collected);
    }

    if (exit_code == 0) {
        if (json_mode) {
            if (first_page_raw && pages_fetched == 1 && !truncated && !unique_flag) {
                output_print_raw_json(first_page_raw);
            } else {
                char *text = reserialize_project_list(&collected);
                if (text) {
                    output_print_raw_json(text);
                    free(text);
                } else {
                    fprintf(stderr, "repoq: out of memory while building JSON output\n");
                    exit_code = 1;
                }
            }
        } else {
            output_print_project_list_table(&collected, use_color);
        }
    }

    free(first_page_raw);
    project_list_free(&collected);
    return exit_code;
}

int main(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"repo", required_argument, NULL, 'r'},
        {"search", required_argument, NULL, 's'},
        {"outdated", no_argument, NULL, 'o'},
        {"limit", required_argument, NULL, 'l'},
        {"json", no_argument, NULL, 'j'},
        {"no-color", no_argument, NULL, 'n'},
        {"unique", no_argument, NULL, 'u'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {NULL, 0, NULL, 0},
    };

    const char *repo_filter = NULL;
    const char *search_term = NULL;
    bool outdated_flag = false;
    bool json_flag = false;
    bool no_color_flag = false;
    bool unique_flag = false;
    bool limit_set = false;
    long limit_value = 0;

    int c;
    while ((c = getopt_long(argc, argv, "r:s:ol:jnuhv", long_opts, NULL)) != -1) {
        switch (c) {
            case 'r':
                repo_filter = optarg;
                break;
            case 's':
                search_term = optarg;
                break;
            case 'o':
                outdated_flag = true;
                break;
            case 'l': {
                errno = 0;
                char *end = NULL;
                long v = strtol(optarg, &end, 10);
                if (errno != 0 || end == optarg || *end != '\0' || v <= 0 || v > INT_MAX) {
                    fprintf(stderr, "repoq: invalid --limit value '%s' (expected a positive integer)\n", optarg);
                    return 2;
                }
                limit_value = v;
                limit_set = true;
                break;
            }
            case 'j':
                json_flag = true;
                break;
            case 'n':
                no_color_flag = true;
                break;
            case 'u':
                unique_flag = true;
                break;
            case 'h':
                print_help();
                return 0;
            case 'v':
                print_version();
                return 0;
            default:
                print_usage(stderr);
                return 2;
        }
    }

    bool has_project_arg = (optind < argc);
    const char *project_name = has_project_arg ? argv[optind] : NULL;
    bool search_mode = (search_term != NULL) || outdated_flag;

    if (has_project_arg && optind + 1 < argc) {
        fprintf(stderr, "repoq: too many arguments\n");
        print_usage(stderr);
        return 2;
    }

    if (has_project_arg && search_mode) {
        fprintf(stderr, "repoq: cannot combine a project name with --search/--outdated\n");
        print_usage(stderr);
        return 2;
    }

    if (!has_project_arg && !search_mode) {
        fprintf(stderr, "repoq: missing project name (or use --search <substring>)\n");
        print_usage(stderr);
        return 2;
    }

    if (repo_filter && search_mode) {
        fprintf(stderr, "repoq: --repo can only be used when querying a single project\n");
        return 2;
    }

    if (limit_set && !search_mode) {
        fprintf(stderr, "repoq: --limit can only be used with --search/--outdated\n");
        return 2;
    }

    bool use_color = output_should_use_color(no_color_flag);

    if (http_global_init() != 0) {
        fprintf(stderr, "repoq: failed to initialize HTTP client\n");
        return 1;
    }

    int rc;
    if (has_project_arg) {
        rc = cmd_project(project_name, repo_filter, json_flag, use_color, unique_flag);
    } else {
        rc = cmd_search(search_term, outdated_flag, limit_value, json_flag, use_color, unique_flag);
    }

    http_global_cleanup();
    return rc;
}

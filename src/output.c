#include "output.h"

#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *RESET_CODE = "\x1b[0m";

static const char *nz(const char *s) {
    return s ? s : "-";
}

/* Picks the most user-facing name to distinguish sibling packages within
 * the same repo (e.g. "frr-bgpd" vs "frr-ospfd" vs "frr", which otherwise
 * all show the exact same repo/version/status and look like duplicate
 * rows). Prefers visiblename (what Repology itself displays), then
 * binname, then srcname. */
static const char *package_display_name(const package_t *p) {
    if (p->visiblename) return p->visiblename;
    if (p->binname) return p->binname;
    if (p->srcname) return p->srcname;
    return NULL;
}

static const char *status_color_code(const char *status) {
    if (!status) {
        return "";
    }
    if (strcmp(status, "newest") == 0 || strcmp(status, "unique") == 0) {
        return "\x1b[32m"; /* green: up to date */
    }
    if (strcmp(status, "outdated") == 0) {
        return "\x1b[31m"; /* red: needs updating */
    }
    if (strcmp(status, "legacy") == 0) {
        return "\x1b[33m"; /* yellow: old, may be intentional */
    }
    if (strcmp(status, "devel") == 0 || strcmp(status, "rolling") == 0) {
        return "\x1b[36m"; /* cyan: bleeding edge / rolling */
    }
    /* noscheme, incorrect, untrusted, ignored, or anything unrecognized */
    return "\x1b[90m"; /* dim gray */
}

static void print_separator(size_t width) {
    for (size_t i = 0; i < width; i++) {
        putchar('-');
    }
    putchar('\n');
}

/* Widens w_* in place to fit p's fields. Shared by both table functions
 * below so their column-sizing passes can't drift apart. */
static void update_widths(const package_t *p, size_t *w_repo, size_t *w_package,
                           size_t *w_version, size_t *w_orig, size_t *w_status) {
    size_t l;
    l = strlen(nz(p->repo));
    if (l > *w_repo) *w_repo = l;
    l = strlen(nz(package_display_name(p)));
    if (l > *w_package) *w_package = l;
    l = strlen(nz(p->version));
    if (l > *w_version) *w_version = l;
    l = strlen(nz(p->origversion));
    if (l > *w_orig) *w_orig = l;
    l = strlen(nz(p->status));
    if (l > *w_status) *w_status = l;
}

/* Prints the REPO/PACKAGE/VERSION/ORIGVERSION/STATUS header row, with an
 * optional leading PROJECT column (used by the --search table). */
static void print_table_header(bool show_project, size_t w_project, size_t w_repo,
                                size_t w_package, size_t w_version, size_t w_orig, size_t w_status) {
    if (show_project) {
        printf("%-*s  ", (int)w_project, "PROJECT");
    }
    printf("%-*s  %-*s  %-*s  %-*s  %-*s\n",
           (int)w_repo, "REPO",
           (int)w_package, "PACKAGE",
           (int)w_version, "VERSION",
           (int)w_orig, "ORIGVERSION",
           (int)w_status, "STATUS");
}

/* Prints one data row. `project` is only used (and may be NULL) when
 * show_project is true. When use_color is false, color/reset are empty
 * strings, so this same call also serves as the uncolored rendering
 * path. */
static void print_table_row(bool show_project, const char *project, size_t w_project,
                             const package_t *p, size_t w_repo, size_t w_package,
                             size_t w_version, size_t w_orig, size_t w_status, bool use_color) {
    if (show_project) {
        printf("%-*s  ", (int)w_project, nz(project));
    }
    const char *color = use_color ? status_color_code(p->status) : "";
    const char *reset = use_color ? RESET_CODE : "";
    printf("%-*s  %-*s  %-*s  %-*s  %s%-*s%s\n",
           (int)w_repo, nz(p->repo),
           (int)w_package, nz(package_display_name(p)),
           (int)w_version, nz(p->version),
           (int)w_orig, nz(p->origversion),
           color, (int)w_status, nz(p->status), reset);
}

bool output_should_use_color(color_mode_t mode) {
    switch (mode) {
        case COLOR_ALWAYS:
            return true;
        case COLOR_NEVER:
            return false;
        case COLOR_AUTO:
        default:
            return isatty(STDOUT_FILENO) != 0;
    }
}

/* Finds the version string Repology has classified as authoritative for
 * this project: the version shared by every package whose status is
 * "newest", or, failing that (a project tracked in only one repository
 * has nothing to compare against, so Repology marks it "unique" instead),
 * the version of a "unique" package. Returns NULL if neither applies. */
static const char *find_tracked_latest_version(const package_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].status && strcmp(list->items[i].status, "newest") == 0) {
            return list->items[i].version;
        }
    }
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].status && strcmp(list->items[i].status, "unique") == 0) {
            return list->items[i].version;
        }
    }
    return NULL;
}

/* Repology's API has no homepage/VCS field for a project (verified against
 * its documented schema), so the closest thing to "this project's address"
 * this tool can offer is a link to Repology's own page for it. */
static void print_project_summary(const char *project_name, const package_list_t *list, bool use_color) {
    const char *latest = find_tracked_latest_version(list);
    if (latest) {
        const char *color = use_color ? status_color_code("newest") : "";
        const char *reset = use_color ? RESET_CODE : "";
        printf("Latest tracked version: %s%s%s\n", color, latest, reset);
    }

    char *enc = url_encode(project_name);
    printf("Repology project page:  https://repology.org/project/%s/versions\n\n", enc ? enc : project_name);
    free(enc);
}

void output_print_package_table(const package_list_t *list, const char *project_name, const char *repo_filter, bool use_color) {
    if (!list || list->count == 0) {
        printf("(no packages found)\n");
        return;
    }

    if (project_name) {
        print_project_summary(project_name, list, use_color);
    }

    size_t w_repo = strlen("REPO");
    size_t w_package = strlen("PACKAGE");
    size_t w_version = strlen("VERSION");
    size_t w_orig = strlen("ORIGVERSION");
    size_t w_status = strlen("STATUS");
    size_t shown = 0;

    for (size_t i = 0; i < list->count; i++) {
        const package_t *p = &list->items[i];
        if (repo_filter && (!p->repo || strcmp(p->repo, repo_filter) != 0)) {
            continue;
        }
        shown++;
        update_widths(p, &w_repo, &w_package, &w_version, &w_orig, &w_status);
    }

    if (shown == 0) {
        if (repo_filter) {
            printf("(no packages found in repo '%s')\n", repo_filter);
        } else {
            printf("(no packages found)\n");
        }
        return;
    }

    print_table_header(false, 0, w_repo, w_package, w_version, w_orig, w_status);
    print_separator(w_repo + w_package + w_version + w_orig + w_status + 8);

    for (size_t i = 0; i < list->count; i++) {
        const package_t *p = &list->items[i];
        if (repo_filter && (!p->repo || strcmp(p->repo, repo_filter) != 0)) {
            continue;
        }
        print_table_row(false, NULL, 0, p, w_repo, w_package, w_version, w_orig, w_status, use_color);
    }
}

void output_print_project_list_table(const project_list_t *list, bool use_color) {
    if (!list || list->count == 0) {
        printf("(no projects found)\n");
        return;
    }

    size_t w_project = strlen("PROJECT");
    size_t w_repo = strlen("REPO");
    size_t w_package = strlen("PACKAGE");
    size_t w_version = strlen("VERSION");
    size_t w_orig = strlen("ORIGVERSION");
    size_t w_status = strlen("STATUS");
    size_t row_count = 0;

    for (size_t i = 0; i < list->count; i++) {
        const named_project_t *np = &list->items[i];
        size_t l = strlen(nz(np->name));
        if (l > w_project) w_project = l;
        for (size_t j = 0; j < np->packages.count; j++) {
            row_count++;
            update_widths(&np->packages.items[j], &w_repo, &w_package, &w_version, &w_orig, &w_status);
        }
    }

    if (row_count == 0) {
        printf("(no packages found)\n");
        return;
    }

    print_table_header(true, w_project, w_repo, w_package, w_version, w_orig, w_status);
    print_separator(w_project + w_repo + w_package + w_version + w_orig + w_status + 10);

    for (size_t i = 0; i < list->count; i++) {
        const named_project_t *np = &list->items[i];
        for (size_t j = 0; j < np->packages.count; j++) {
            print_table_row(true, np->name, w_project, &np->packages.items[j],
                             w_repo, w_package, w_version, w_orig, w_status, use_color);
        }
    }

    printf("\n%zu project(s), %zu package row(s)\n", list->count, row_count);
}

void output_print_raw_json(const char *json_text) {
    if (!json_text) {
        return;
    }
    fputs(json_text, stdout);
    size_t len = strlen(json_text);
    if (len == 0 || json_text[len - 1] != '\n') {
        fputc('\n', stdout);
    }
}

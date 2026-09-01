#include "output.h"

#include <stdio.h>
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

bool output_should_use_color(bool no_color_flag) {
    if (no_color_flag) {
        return false;
    }
    return isatty(STDOUT_FILENO) != 0;
}

void output_print_package_table(const package_list_t *list, const char *repo_filter, bool use_color) {
    if (!list || list->count == 0) {
        printf("(no packages found)\n");
        return;
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
        size_t l;
        l = strlen(nz(p->repo));
        if (l > w_repo) w_repo = l;
        l = strlen(nz(package_display_name(p)));
        if (l > w_package) w_package = l;
        l = strlen(nz(p->version));
        if (l > w_version) w_version = l;
        l = strlen(nz(p->origversion));
        if (l > w_orig) w_orig = l;
        l = strlen(nz(p->status));
        if (l > w_status) w_status = l;
    }

    if (shown == 0) {
        if (repo_filter) {
            printf("(no packages found in repo '%s')\n", repo_filter);
        } else {
            printf("(no packages found)\n");
        }
        return;
    }

    printf("%-*s  %-*s  %-*s  %-*s  %-*s\n",
           (int)w_repo, "REPO",
           (int)w_package, "PACKAGE",
           (int)w_version, "VERSION",
           (int)w_orig, "ORIGVERSION",
           (int)w_status, "STATUS");
    print_separator(w_repo + w_package + w_version + w_orig + w_status + 8);

    for (size_t i = 0; i < list->count; i++) {
        const package_t *p = &list->items[i];
        if (repo_filter && (!p->repo || strcmp(p->repo, repo_filter) != 0)) {
            continue;
        }
        if (use_color) {
            printf("%-*s  %-*s  %-*s  %-*s  %s%-*s%s\n",
                   (int)w_repo, nz(p->repo),
                   (int)w_package, nz(package_display_name(p)),
                   (int)w_version, nz(p->version),
                   (int)w_orig, nz(p->origversion),
                   status_color_code(p->status), (int)w_status, nz(p->status), RESET_CODE);
        } else {
            printf("%-*s  %-*s  %-*s  %-*s  %-*s\n",
                   (int)w_repo, nz(p->repo),
                   (int)w_package, nz(package_display_name(p)),
                   (int)w_version, nz(p->version),
                   (int)w_orig, nz(p->origversion),
                   (int)w_status, nz(p->status));
        }
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
            const package_t *p = &np->packages.items[j];
            row_count++;
            l = strlen(nz(p->repo));
            if (l > w_repo) w_repo = l;
            l = strlen(nz(package_display_name(p)));
            if (l > w_package) w_package = l;
            l = strlen(nz(p->version));
            if (l > w_version) w_version = l;
            l = strlen(nz(p->origversion));
            if (l > w_orig) w_orig = l;
            l = strlen(nz(p->status));
            if (l > w_status) w_status = l;
        }
    }

    if (row_count == 0) {
        printf("(no packages found)\n");
        return;
    }

    printf("%-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n",
           (int)w_project, "PROJECT",
           (int)w_repo, "REPO",
           (int)w_package, "PACKAGE",
           (int)w_version, "VERSION",
           (int)w_orig, "ORIGVERSION",
           (int)w_status, "STATUS");
    print_separator(w_project + w_repo + w_package + w_version + w_orig + w_status + 10);

    for (size_t i = 0; i < list->count; i++) {
        const named_project_t *np = &list->items[i];
        for (size_t j = 0; j < np->packages.count; j++) {
            const package_t *p = &np->packages.items[j];
            if (use_color) {
                printf("%-*s  %-*s  %-*s  %-*s  %-*s  %s%-*s%s\n",
                       (int)w_project, nz(np->name),
                       (int)w_repo, nz(p->repo),
                       (int)w_package, nz(package_display_name(p)),
                       (int)w_version, nz(p->version),
                       (int)w_orig, nz(p->origversion),
                       status_color_code(p->status), (int)w_status, nz(p->status), RESET_CODE);
            } else {
                printf("%-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n",
                       (int)w_project, nz(np->name),
                       (int)w_repo, nz(p->repo),
                       (int)w_package, nz(package_display_name(p)),
                       (int)w_version, nz(p->version),
                       (int)w_orig, nz(p->origversion),
                       (int)w_status, nz(p->status));
            }
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

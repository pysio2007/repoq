#ifndef REPOQ_OUTPUT_H
#define REPOQ_OUTPUT_H

#include "json_parse.h"

#include <stdbool.h>

/* COLOR_ALWAYS: color unconditionally, even when piped -- e.g. into
 *   `bat`, which renders incoming ANSI color codes instead of stripping
 *   them, unlike a plain `less` (needs -R) or a non-terminal file. This
 *   is repoq's default (see main.c).
 * COLOR_AUTO: color only when stdout is a terminal.
 * COLOR_NEVER: never color, regardless of stdout. */
typedef enum {
    COLOR_AUTO,
    COLOR_ALWAYS,
    COLOR_NEVER,
} color_mode_t;

/* Resolves a color_mode_t to an actual yes/no decision for this run. */
bool output_should_use_color(color_mode_t mode);

/* Prints an aligned table of packages for a single project, preceded by a
 * summary line with the version Repology tracks as newest for it and a
 * link to its Repology project page. If repo_filter is non-NULL, only
 * packages whose repo matches it exactly are shown in the table (the
 * summary line is unaffected). project_name may be NULL to skip the
 * summary line. */
void output_print_package_table(const package_list_t *list, const char *project_name, const char *repo_filter, bool use_color);

/* Prints an aligned table of packages across many projects, one row per
 * package, with a leading PROJECT column. Used for --search results. */
void output_print_project_list_table(const project_list_t *list, bool use_color);

/* Writes a JSON string to stdout verbatim, followed by a newline if one
 * isn't already present. */
void output_print_raw_json(const char *json_text);

#endif /* REPOQ_OUTPUT_H */

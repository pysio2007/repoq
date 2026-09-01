#ifndef REPOQ_OUTPUT_H
#define REPOQ_OUTPUT_H

#include "json_parse.h"

#include <stdbool.h>

/* Decides whether colored output should be used: an explicit --no-color
 * flag always disables it; otherwise color is only used when stdout is
 * connected to a terminal (auto-disabled when piped/redirected). */
bool output_should_use_color(bool no_color_flag);

/* Prints an aligned table of packages for a single project. If
 * repo_filter is non-NULL, only packages whose repo matches it exactly
 * are shown. */
void output_print_package_table(const package_list_t *list, const char *repo_filter, bool use_color);

/* Prints an aligned table of packages across many projects, one row per
 * package, with a leading PROJECT column. Used for --search results. */
void output_print_project_list_table(const project_list_t *list, bool use_color);

/* Writes a JSON string to stdout verbatim, followed by a newline if one
 * isn't already present. */
void output_print_raw_json(const char *json_text);

#endif /* REPOQ_OUTPUT_H */

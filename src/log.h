#ifndef GBOYC_LOG_H
#define GBOYC_LOG_H

#include <stdbool.h>

/*
 * Single place for console output. Every call appends a newline.
 *   info    -> stdout, no prefix
 *   verbose -> stderr, "[gboyc] " prefix, only with --verbose
 *   error   -> stderr, "gboyc: " prefix
 */

void log_set_verbose(bool on);
bool log_verbose_enabled(void);

void log_verbose(const char *fmt, ...); /* only printed with --verbose */
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif

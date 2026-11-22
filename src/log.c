#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static bool verbose_on;

void log_set_verbose(bool on) {
	verbose_on = on;
}

bool log_verbose_enabled(void) {
	return verbose_on;
}

static void vlog(FILE *out, const char *prefix, const char *fmt, va_list args) {
	fputs(prefix, out);
	vfprintf(out, fmt, args);
	fputc('\n', out);
}

void log_verbose(const char *fmt, ...) {
	if (!verbose_on) {
		return;
	}

	va_list args;

	va_start(args, fmt);
	vlog(stderr, "[gboyc] ", fmt, args);
	va_end(args);
}

void log_info(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vlog(stdout, "", fmt, args);
	va_end(args);
}

void log_error(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vlog(stderr, "gboyc: ", fmt, args);
	va_end(args);
}

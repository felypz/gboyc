#include "verbose.h"

#include <stdarg.h>
#include <stdio.h>

static bool enabled;

void verbose_enable(bool on) {
	enabled = on;
}

bool verbose_enabled(void) {
	return enabled;
}

void verbose(const char *fmt, ...) {
	if (!enabled) {
		return;
	}

	va_list args;

	fputs("[gboyc] ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

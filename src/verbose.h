#ifndef GBOYC_VERBOSE_H
#define GBOYC_VERBOSE_H

#include <stdbool.h>

void verbose_enable(bool on);
bool verbose_enabled(void);
void verbose(const char *fmt, ...);

#endif

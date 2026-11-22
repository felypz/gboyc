#ifndef GBOYC_CRASH_H
#define GBOYC_CRASH_H

#include <stdbool.h>

#include "crash_report.h"
#include "gb.h"

bool crash_detect_game(const Gb *gb, CrashReport *r);
void crash_emulator(CrashReport *r, const Gb *gb, const char *reason, const char *detail);
void crash_print(const CrashReport *r, const Gb *gb, bool full);

#endif

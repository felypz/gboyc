#ifndef GBOYC_CRASH_H
#define GBOYC_CRASH_H

#include <stdbool.h>

#include "gb.h"

#define CRASH_LINES	   6
#define CRASH_LINE_LEN 64

typedef enum {
	CRASH_GAME,
	CRASH_EMULATOR,
} CrashKind;

typedef struct {
	CrashKind kind;
	char title[24];
	char reason[CRASH_LINE_LEN];
	char lines[CRASH_LINES][CRASH_LINE_LEN];
	int line_count;
} CrashReport;

bool crash_detect_game(const Gb *gb, CrashReport *r);
void crash_emulator(CrashReport *r, const Gb *gb, const char *reason, const char *detail);
void crash_print(const CrashReport *r, const Gb *gb, bool full);

#endif

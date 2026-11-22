#ifndef GBOYC_CRASH_REPORT_H
#define GBOYC_CRASH_REPORT_H

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

#endif

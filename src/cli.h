#ifndef GBOYC_CLI_H
#define GBOYC_CLI_H

#include <stdbool.h>

/* Failures that --crash-test can inject, to exercise the crash screens. */
typedef enum {
	CRASH_TEST_NONE,
	CRASH_TEST_SEGV,
	CRASH_TEST_ABORT,
	CRASH_TEST_FPE,
	CRASH_TEST_ILLEGAL,
	CRASH_TEST_HALT,
	CRASH_TEST_COUNT,
} CrashTest;

typedef struct {
	const char *rom;
	const char *boot_rom; /* --boot-rom <file> */
	CrashTest crash_test; /* --crash-test <kind> */
	bool no_boot;
	bool verbose;
	bool mute;
	bool paused;
	int scale;
	int load_state_slot; /* --state <slot>, or 0 when not given */
} CliOptions;

typedef enum {
	CLI_RUN,
	CLI_EXIT_OK,
	CLI_EXIT_FAIL,
} CliAction;

const char *cli_crash_test_name(CrashTest test);
CliAction cli_parse(CliOptions *o, int argc, char **argv);

#endif

#ifndef GBOYC_CLI_H
#define GBOYC_CLI_H

#include <stdbool.h>

#ifndef GBOYC_VERSION
#define GBOYC_VERSION "0.1.0"
#endif

typedef struct {
	const char *rom;
	const char *boot_rom;
	const char *crash_test;
	bool no_boot;
	bool verbose;
	bool mute;
	bool paused;
	int scale;
	int state;
} CliOptions;

typedef enum {
	CLI_RUN,
	CLI_EXIT_OK,
	CLI_EXIT_FAIL,
} CliAction;

CliAction cli_parse(CliOptions *o, int argc, char **argv);

#endif

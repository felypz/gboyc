#include "cli.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "paths.h"

#define SCALE_MIN	  1
#define SCALE_MAX	  10
#define SCALE_DEFAULT 4
#define SLOT_MIN	  1
#define SLOT_MAX	  9

enum {
	OPT_NO_BOOT = 256,
	OPT_BOOT_ROM,
	OPT_MUTE,
	OPT_PAUSED,
	OPT_STATE,
};

static const char SHORT_OPTS[] = "r:s:vpVh";

static const struct option LONG_OPTS[] = {
	{"rom", required_argument, NULL, 'r'},
	{"no-boot", no_argument, NULL, OPT_NO_BOOT},
	{"boot-rom", required_argument, NULL, OPT_BOOT_ROM},
	{"scale", required_argument, NULL, 's'},
	{"mute", no_argument, NULL, OPT_MUTE},
	{"paused", no_argument, NULL, OPT_PAUSED},
	{"state", required_argument, NULL, OPT_STATE},
	{"verbose", no_argument, NULL, 'v'},
	{"paths", no_argument, NULL, 'p'},
	{"version", no_argument, NULL, 'V'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0},
};

static void print_help(void) {
	fputs("Usage: gboyc [OPTIONS] [ROM]\n"
		  "\n"
		  "A Game Boy (DMG) emulator.\n"
		  "\n"
		  "Arguments:\n"
		  "  ROM                  ROM file to load (same as --rom)\n"
		  "\n"
		  "Options:\n"
		  "  -r, --rom PATH       Load this ROM at startup\n"
		  "      --no-boot        Skip the boot animation for this run only\n"
		  "      --boot-rom PATH  Use this boot ROM for this run only\n"
		  "  -s, --scale N        Initial window size, 1-10 (default 4)\n"
		  "      --mute           Disable audio\n"
		  "      --paused         Start paused (needs a ROM)\n"
		  "      --state SLOT     Load save state SLOT (1-9) at startup (needs a ROM)\n"
		  "  -v, --verbose        Print what the emulator is doing to stderr\n"
		  "  -p, --paths          Print the config and data directories, then exit\n"
		  "  -V, --version        Print the version, then exit\n"
		  "  -h, --help           Print this help, then exit\n",
		  stdout);
}

static void print_paths(void) {
	char path[PATHS_MAX];

	if (paths_dir(PATHS_CONFIG, path, sizeof path)) {
		printf("config: %s  (settings.cfg)\n", path);
	}
	if (paths_dir(PATHS_DATA, path, sizeof path)) {
		printf("data:   %s  (boot/, saves/)\n", path);
	}
}

static CliAction fail(const char *message, const char *arg) {
	fprintf(stderr, "gboyc: %s%s%s%s\n", message, arg ? " '" : "", arg ? arg : "", arg ? "'" : "");
	fputs("Try 'gboyc --help' for more information.\n", stderr);
	return CLI_EXIT_FAIL;
}

static bool parse_number(const char *text, int min, int max, int *out) {
	char *end;
	long value = strtol(text, &end, 10);

	if (end == text || *end != '\0' || value < min || value > max) {
		return false;
	}
	*out = (int)value;
	return true;
}

CliAction cli_parse(CliOptions *o, int argc, char **argv) {
	*o = (CliOptions){.scale = SCALE_DEFAULT};

	int c;

	while ((c = getopt_long(argc, argv, SHORT_OPTS, LONG_OPTS, NULL)) != -1) {
		switch (c) {
		case 'r':
			if (o->rom) {
				return fail("ROM given more than once", NULL);
			}
			o->rom = optarg;
			break;
		case 's':
			if (!parse_number(optarg, SCALE_MIN, SCALE_MAX, &o->scale)) {
				return fail("invalid scale (expected 1-10)", optarg);
			}
			break;
		case OPT_NO_BOOT:
			o->no_boot = true;
			break;
		case OPT_BOOT_ROM:
			o->boot_rom = optarg;
			break;
		case OPT_MUTE:
			o->mute = true;
			break;
		case OPT_PAUSED:
			o->paused = true;
			break;
		case OPT_STATE:
			if (!parse_number(optarg, SLOT_MIN, SLOT_MAX, &o->state)) {
				return fail("invalid state slot (expected 1-9)", optarg);
			}
			break;
		case 'v':
			o->verbose = true;
			break;
		case 'p':
			print_paths();
			return CLI_EXIT_OK;
		case 'V':
			printf("gboyc %s\n", GBOYC_VERSION);
			return CLI_EXIT_OK;
		case 'h':
			print_help();
			return CLI_EXIT_OK;
		default:
			fputs("Try 'gboyc --help' for more information.\n", stderr);
			return CLI_EXIT_FAIL;
		}
	}

	int extra = argc - optind;

	if (extra > 1) {
		return fail("too many arguments", argv[optind + 1]);
	}
	if (extra == 1) {
		if (o->rom) {
			return fail("ROM given more than once", argv[optind]);
		}
		o->rom = argv[optind];
	}

	if (o->no_boot && o->boot_rom) {
		return fail("--no-boot and --boot-rom cannot be combined", NULL);
	}
	if ((o->paused || o->state) && !o->rom) {
		return fail("--paused and --state need a ROM", NULL);
	}
	return CLI_RUN;
}

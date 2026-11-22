#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "file_util.h"
#include "paths.h"
#include "platform.h"

#define SETTINGS_FILE		  "settings.cfg"
#define DEFAULT_BOOT_ROM_FILE "dmg_boot.bin"

void settings_defaults(Settings *s) {
	memset(s, 0, sizeof *s);
	s->boot_rom_enabled = true;
	snprintf(s->boot_rom_file, sizeof s->boot_rom_file, "%s", DEFAULT_BOOT_ROM_FILE);
	s->theme = THEME_AUTO;
	platform_default_pad(&s->bindings);
	platform_default_shortcuts(&s->bindings);
}

/* The boot ROM is looked up inside the data dir, so it must be a bare file name. */
static bool valid_file_name(const char *name) {
	size_t len = strlen(name);

	return len > 0 && len < SETTINGS_NAME_MAX && name[0] != '.' && !strchr(name, '/') &&
		   !strchr(name, '\\');
}

static void parse_line(Settings *s, char *line) {
	line[strcspn(line, "\r\n")] = '\0';

	char *eq = strchr(line, '=');

	if (!eq) {
		return;
	}
	*eq = '\0';

	const char *name = line;
	const char *value = eq + 1;

	if (strcmp(name, "boot_rom") == 0) {
		s->boot_rom_enabled = strcmp(value, "off") != 0;
	} else if (strcmp(name, "boot_rom_file") == 0) {
		if (valid_file_name(value)) {
			snprintf(s->boot_rom_file, sizeof s->boot_rom_file, "%s", value);
		}
	} else if (strcmp(name, "theme") == 0) {
		s->theme = theme_mode_parse(value);
	} else {
		bindings_parse(&s->bindings, name, value);
	}
}

void settings_load(Settings *s) {
	settings_defaults(s);

	char path[PATHS_MAX];

	if (!paths_file(PATHS_CONFIG, path, sizeof path, NULL, SETTINGS_FILE)) {
		return;
	}

	FILE *f = fopen(path, "r");

	if (!f) {
		return;
	}

	char line[256];

	while (fgets(line, sizeof line, f)) {
		parse_line(s, line);
	}
	fclose(f);
}

bool settings_save(const Settings *s) {
	char path[PATHS_MAX];
	AtomicFile out;

	if (!paths_make(PATHS_CONFIG, NULL) ||
		!paths_file(PATHS_CONFIG, path, sizeof path, NULL, SETTINGS_FILE) ||
		!atomic_file_open(&out, path, "w")) {
		return false;
	}

	fprintf(out.file, "boot_rom=%s\nboot_rom_file=%s\ntheme=%s\n",
			s->boot_rom_enabled ? "on" : "off", s->boot_rom_file, theme_mode_key(s->theme));
	bindings_write(&s->bindings, out.file);
	return atomic_file_commit(&out);
}

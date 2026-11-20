#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "paths.h"
#include "platform.h"

#define SETTINGS_FILE	  "settings.cfg"
#define DEFAULT_BOOT_FILE "dmg_boot.bin"

void settings_defaults(Settings *s) {
	memset(s, 0, sizeof *s);
	s->boot_enabled = true;
	snprintf(s->boot_file, sizeof s->boot_file, "%s", DEFAULT_BOOT_FILE);
	s->theme = THEME_AUTO;
	platform_default_pad(&s->bindings);
	platform_default_shortcuts(&s->bindings);
}

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

	if (strcmp(name, "boot") == 0) {
		s->boot_enabled = strcmp(value, "off") != 0;
	} else if (strcmp(name, "boot_file") == 0) {
		if (valid_file_name(value)) {
			snprintf(s->boot_file, sizeof s->boot_file, "%s", value);
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
	char tmp[PATHS_MAX + 8];

	if (!paths_make(PATHS_CONFIG, NULL) ||
		!paths_file(PATHS_CONFIG, path, sizeof path, NULL, SETTINGS_FILE)) {
		return false;
	}
	snprintf(tmp, sizeof tmp, "%s.tmp", path);

	FILE *f = fopen(tmp, "w");
	if (!f) {
		return false;
	}

	fprintf(f, "boot=%s\nboot_file=%s\ntheme=%s\n", s->boot_enabled ? "on" : "off", s->boot_file,
			theme_mode_key(s->theme));
	bindings_write(&s->bindings, f);

	bool ok = fclose(f) == 0 && rename(tmp, path) == 0;
	if (!ok) {
		remove(tmp);
	}
	return ok;
}

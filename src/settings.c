#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"

#define SETTINGS_DIR	  "config"
#define SETTINGS_PATH	  "config/settings.cfg"
#define DEFAULT_BOOT_FILE "dmg_boot.bin"

static const char *const BUTTON_NAMES[JOY_BUTTONS] = {
	"right", "left", "up", "down", "a", "b", "select", "start",
};

void settings_defaults(Settings *s) {
	memset(s, 0, sizeof *s);
	s->boot_enabled = true;
	snprintf(s->boot_file, sizeof s->boot_file, "%s", DEFAULT_BOOT_FILE);
	platform_default_keymap(&s->keymap);
}

static bool valid_file_name(const char *name) {
	size_t len = strlen(name);

	return len > 0 && len < SETTINGS_NAME_MAX && name[0] != '.' && !strchr(name, '/') &&
		   !strchr(name, '\\');
}

static void parse_keys(Settings *s, const char *button, const char *value) {
	int first;
	int second;

	if (sscanf(value, "%d,%d", &first, &second) != 2 || !platform_key_valid(first) ||
		!platform_key_valid(second)) {
		return;
	}

	for (int i = 0; i < JOY_BUTTONS; i++) {
		if (strcmp(button, BUTTON_NAMES[i]) == 0) {
			s->keymap.key[i][0] = first;
			s->keymap.key[i][1] = second;
		}
	}
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
	} else if (strncmp(name, "joy.", 4) == 0) {
		parse_keys(s, name + 4, value);
	}
}

void settings_load(Settings *s) {
	settings_defaults(s);

	FILE *f = fopen(SETTINGS_PATH, "r");
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
	if (!platform_make_dir(SETTINGS_DIR)) {
		return false;
	}

	char tmp[sizeof SETTINGS_PATH + 8];
	snprintf(tmp, sizeof tmp, "%s.tmp", SETTINGS_PATH);

	FILE *f = fopen(tmp, "w");
	if (!f) {
		return false;
	}

	fprintf(f, "boot=%s\nboot_file=%s\n", s->boot_enabled ? "on" : "off", s->boot_file);
	for (int i = 0; i < JOY_BUTTONS; i++) {
		fprintf(f, "joy.%s=%d,%d\n", BUTTON_NAMES[i], s->keymap.key[i][0], s->keymap.key[i][1]);
	}

	bool ok = fclose(f) == 0 && rename(tmp, SETTINGS_PATH) == 0;
	if (!ok) {
		remove(tmp);
	}
	return ok;
}

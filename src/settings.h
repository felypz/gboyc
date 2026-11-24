#ifndef GBOYC_SETTINGS_H
#define GBOYC_SETTINGS_H

#include <stdbool.h>

#include "bindings.h"
#include "theme.h"

#define SETTINGS_NAME_MAX 64

typedef struct {
	bool boot_rom_enabled;
	char boot_rom_file[SETTINGS_NAME_MAX];
	ThemeMode theme;
	int players; /* 1, or 2 for two linked consoles side by side */
	Bindings bindings;
} Settings;

void settings_defaults(Settings *s);
void settings_load(Settings *s);
bool settings_save(const Settings *s);

#endif

#ifndef GBOYC_SETTINGS_H
#define GBOYC_SETTINGS_H

#include <stdbool.h>

#include "bindings.h"
#include "theme.h"

#define SETTINGS_NAME_MAX 64

typedef struct {
	bool boot_enabled;
	char boot_file[SETTINGS_NAME_MAX];
	ThemeMode theme;
	Bindings bindings;
} Settings;

void settings_defaults(Settings *s);
void settings_load(Settings *s);
bool settings_save(const Settings *s);

#endif

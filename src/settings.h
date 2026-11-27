#ifndef GBOYC_SETTINGS_H
#define GBOYC_SETTINGS_H

#include <stdbool.h>

#include "bindings.h"
#include "display.h"
#include "theme.h"

#define SETTINGS_NAME_MAX	  64
#define SETTINGS_TURBO_MIN	  2
#define SETTINGS_TURBO_MAX	  8
#define SETTINGS_VOLUME_MAX	  100
#define SETTINGS_ALL_CHANNELS 0xFu

typedef struct {
	bool boot_rom_enabled;
	char boot_rom_file[SETTINGS_NAME_MAX];
	ThemeMode theme;
	int players; /* 1, or 2 for two linked consoles side by side */
	Palette palette;
	Ghosting ghosting;
	bool smooth;	   /* bilinear scaling instead of sharp pixels */
	int turbo;		   /* speed multiplier of the speed shortcut */
	int volume;		   /* 0-100 */
	unsigned channels; /* bit i set = sound channel i + 1 is audible */
	Bindings bindings;
} Settings;

void settings_defaults(Settings *s);
void settings_load(Settings *s);
bool settings_save(const Settings *s);

#endif

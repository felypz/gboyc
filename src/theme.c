#include "theme.h"

#include <string.h>

static const Theme DARK = {
	.bg = 0xFF121614,
	.surface = 0xFF1D2321,
	.line = 0xFF2C3532,
	.text = 0xFFE8EDE6,
	.dim = 0xFF8C9A92,
	.accent = 0xFF9BBC0F,
	.on_accent = 0xFF0F380F,
	.warn = 0xFFF2B84B,
};

static const Theme LIGHT = {
	.bg = 0xFFEEF1E6,
	.surface = 0xFFFFFFFF,
	.line = 0xFFD3DACB,
	.text = 0xFF18211B,
	.dim = 0xFF68756B,
	.accent = 0xFF306230,
	.on_accent = 0xFFF4F7EA,
	.warn = 0xFFB45309,
};

static const char *const NAMES[THEME_COUNT] = {"Auto", "Dark", "Light"};
static const char *const KEYS[THEME_COUNT] = {"auto", "dark", "light"};

const Theme *theme_get(ThemeMode mode, bool system_dark) {
	bool dark = mode == THEME_DARK || (mode == THEME_AUTO && system_dark);

	return dark ? &DARK : &LIGHT;
}

const char *theme_mode_name(ThemeMode mode) {
	return NAMES[mode];
}

const char *theme_mode_key(ThemeMode mode) {
	return KEYS[mode];
}

ThemeMode theme_mode_parse(const char *key) {
	for (int i = 0; i < THEME_COUNT; i++) {
		if (strcmp(key, KEYS[i]) == 0) {
			return (ThemeMode)i;
		}
	}
	return THEME_AUTO;
}

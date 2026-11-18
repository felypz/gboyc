#ifndef GBOYC_THEME_H
#define GBOYC_THEME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	THEME_AUTO,
	THEME_DARK,
	THEME_LIGHT,
	THEME_COUNT,
} ThemeMode;

typedef struct {
	uint32_t bg;
	uint32_t surface;
	uint32_t line;
	uint32_t text;
	uint32_t dim;
	uint32_t accent;
	uint32_t on_accent;
	uint32_t warn;
} Theme;

const Theme *theme_get(ThemeMode mode, bool system_dark);
const char *theme_mode_name(ThemeMode mode);
const char *theme_mode_key(ThemeMode mode);
ThemeMode theme_mode_parse(const char *key);

#endif

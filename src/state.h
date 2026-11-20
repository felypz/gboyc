#ifndef GBOYC_STATE_H
#define GBOYC_STATE_H

#include <stdbool.h>
#include <stddef.h>

#include "gb.h"

#define STATE_DIR	   "config/saves"
#define STATE_PATH_MAX 4096

typedef enum {
	STATE_OK,
	STATE_MISSING,
	STATE_INVALID,
} StateResult;

void state_path(char *out, size_t size, const char *rom_path, int slot);
bool state_ensure_dir(void);
bool state_save(const Gb *gb, const char *path);
StateResult state_load(Gb *gb, const char *path);

#endif

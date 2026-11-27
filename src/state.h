#ifndef GBOYC_STATE_H
#define GBOYC_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "gb.h"

typedef enum {
	STATE_OK,
	STATE_MISSING,
	STATE_INVALID,
} StateResult;

/* Builds "<data>/states/<rom name>.state<slot>". Returns false if the path doesn't fit. */
bool state_path(char *out, size_t size, const char *rom_path, int slot);
bool state_ensure_dir(void);
/* Modification time of the state file of a slot; false if there is none. */
bool state_time(const char *rom_path, int slot, time_t *when);
bool state_save(const Gb *gb, const char *path);
StateResult state_load(Gb *gb, const char *path);

#endif

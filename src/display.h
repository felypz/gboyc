#ifndef GBOYC_DISPLAY_H
#define GBOYC_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "ppu.h"

typedef enum {
	PALETTE_CLASSIC,
	PALETTE_GRAY,
	PALETTE_POCKET,
	PALETTE_SEPIA,
	PALETTE_COUNT,
} Palette;

typedef enum {
	GHOST_OFF,
	GHOST_LOW,
	GHOST_HIGH,
	GHOST_COUNT,
} Ghosting;

const char *palette_name(Palette p);
const char *palette_key(Palette p);
Palette palette_parse(const char *key);
const char *ghosting_name(Ghosting g);
const char *ghosting_key(Ghosting g);
Ghosting ghosting_parse(const char *key);

/* Per-console output buffers: recolouring and LCD ghosting happen outside the PPU. */
typedef struct {
	uint32_t out[PPU_W * PPU_H];
	bool has_prev; /* out[] holds the previous frame */
} Display;

void display_reset(Display *d);
/* Returns the frame to show: fb itself when nothing needs to change, else d->out. */
const uint32_t *display_process(Display *d, const uint32_t *fb, Palette palette, Ghosting ghost);

#endif

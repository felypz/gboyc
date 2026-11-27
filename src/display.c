#include "display.h"

#include <string.h>

#include "palette.h"

/* Lightest to darkest. Classic is what the PPU draws. */
static const uint32_t PALETTES[PALETTE_COUNT][4] = {
	[PALETTE_CLASSIC] = {DMG_COLOR_0, DMG_COLOR_1, DMG_COLOR_2, DMG_COLOR_3},
	[PALETTE_GRAY] = {0xFFEFEFEFU, 0xFFB4B4B4U, 0xFF686868U, 0xFF161616U},
	[PALETTE_POCKET] = {0xFFC4CFA1U, 0xFF8B956DU, 0xFF4D533CU, 0xFF1F1F1FU},
	[PALETTE_SEPIA] = {0xFFF0E2B8U, 0xFFC0A070U, 0xFF70502CU, 0xFF281808U},
};

static const char *const NAMES[PALETTE_COUNT] = {"Classic", "Gray", "Pocket", "Sepia"};
static const char *const KEYS[PALETTE_COUNT] = {"classic", "gray", "pocket", "sepia"};
static const char *const GHOST_NAMES[GHOST_COUNT] = {"Off", "Low", "High"};
static const char *const GHOST_KEYS[GHOST_COUNT] = {"off", "low", "high"};

const char *palette_name(Palette p) {
	return NAMES[p];
}

const char *palette_key(Palette p) {
	return KEYS[p];
}

Palette palette_parse(const char *key) {
	for (int i = 0; i < PALETTE_COUNT; i++) {
		if (strcmp(key, KEYS[i]) == 0) {
			return (Palette)i;
		}
	}
	return PALETTE_CLASSIC;
}

const char *ghosting_name(Ghosting g) {
	return GHOST_NAMES[g];
}

const char *ghosting_key(Ghosting g) {
	return GHOST_KEYS[g];
}

Ghosting ghosting_parse(const char *key) {
	for (int i = 0; i < GHOST_COUNT; i++) {
		if (strcmp(key, GHOST_KEYS[i]) == 0) {
			return (Ghosting)i;
		}
	}
	return GHOST_OFF;
}

void display_reset(Display *d) {
	d->has_prev = false;
}

static uint32_t recolor(uint32_t pixel, const uint32_t *to) {
	for (int i = 0; i < 4; i++) {
		if (pixel == PALETTES[PALETTE_CLASSIC][i]) {
			return to[i];
		}
	}
	return pixel;
}

/* (a * (4 - k) + b * k) / 4 on every channel. */
static uint32_t blend(uint32_t a, uint32_t b, unsigned k) {
	uint32_t out = 0xFF000000U;

	for (int shift = 0; shift < 24; shift += 8) {
		unsigned ca = (a >> shift) & 0xFF;
		unsigned cb = (b >> shift) & 0xFF;

		out |= ((ca * (4 - k) + cb * k) / 4) << shift;
	}
	return out;
}

const uint32_t *display_process(Display *d, const uint32_t *fb, Palette palette, Ghosting ghost) {
	if (palette == PALETTE_CLASSIC && ghost == GHOST_OFF) {
		d->has_prev = false;
		return fb;
	}

	unsigned k = ghost == GHOST_HIGH ? 2 : (ghost == GHOST_LOW ? 1 : 0);
	bool mix = k && d->has_prev;

	for (int i = 0; i < PPU_W * PPU_H; i++) {
		uint32_t pixel = recolor(fb[i], PALETTES[palette]);

		d->out[i] = mix ? blend(pixel, d->out[i], k) : pixel;
	}
	d->has_prev = true;
	return d->out;
}

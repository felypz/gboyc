#include "state.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATE_MAGIC	  "GBOYCST"
#define STATE_VERSION 1

typedef struct {
	char magic[8];
	uint32_t version;
	uint32_t gb_size;
	uint32_t cart_size;
	uint32_t cart_id;
} StateHeader;

void state_path(char *out, size_t size, const char *rom_path, int slot) {
	const char *name = strrchr(rom_path, '/');
	name = name ? name + 1 : rom_path;

	size_t len = strlen(name);
	const char *dot = strrchr(name, '.');

	if (dot && dot != name) {
		len = (size_t)(dot - name);
	}
	snprintf(out, size, "%s/%.*s.state%d", STATE_DIR, (int)len, name, slot);
}

bool state_save(const Gb *gb, const char *path) {
	size_t cart_size = cartridge_state_size(gb->cart);
	size_t total = sizeof(StateHeader) + sizeof *gb + cart_size;
	uint8_t *buf = malloc(total);

	if (!buf) {
		return false;
	}

	StateHeader h = {
		.version = STATE_VERSION,
		.gb_size = (uint32_t)sizeof *gb,
		.cart_size = (uint32_t)cart_size,
		.cart_id = cartridge_id(gb->cart),
	};
	memcpy(h.magic, STATE_MAGIC, sizeof h.magic);
	memcpy(buf, &h, sizeof h);
	memcpy(buf + sizeof h, gb, sizeof *gb);
	cartridge_state_write(gb->cart, buf + sizeof h + sizeof *gb);

	char tmp[STATE_PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);

	FILE *f = fopen(tmp, "wb");
	bool ok = f && fwrite(buf, 1, total, f) == total;

	if (f) {
		ok = (fclose(f) == 0) && ok;
	}
	ok = ok && rename(tmp, path) == 0;
	if (!ok) {
		remove(tmp);
	}

	free(buf);
	return ok;
}

StateResult state_load(Gb *gb, const char *path) {
	FILE *f = fopen(path, "rb");

	if (!f) {
		return STATE_MISSING;
	}

	size_t cart_size = cartridge_state_size(gb->cart);
	size_t total = sizeof(StateHeader) + sizeof *gb + cart_size;
	uint8_t *buf = malloc(total + 1);

	if (!buf) {
		fclose(f);
		return STATE_INVALID;
	}

	size_t n = fread(buf, 1, total + 1, f);
	fclose(f);

	StateResult result = STATE_INVALID;
	StateHeader h;

	if (n == total) {
		memcpy(&h, buf, sizeof h);
		if (memcmp(h.magic, STATE_MAGIC, sizeof h.magic) == 0 && h.version == STATE_VERSION &&
			h.gb_size == sizeof *gb && h.cart_size == cart_size &&
			h.cart_id == cartridge_id(gb->cart)) {
			Cartridge *cart = gb->cart;

			memcpy(gb, buf + sizeof h, sizeof *gb);
			gb->cart = cart;
			gb->cpu.gb = gb;
			cartridge_state_read(cart, buf + sizeof h + sizeof *gb, cart_size);
			result = STATE_OK;
		}
	}

	free(buf);
	return result;
}

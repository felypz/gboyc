#include "state.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paths.h"
#include "verbose.h"

#define STATE_MAGIC	  "GBOYCST"
#define STATE_VERSION 2
#define STATE_SUBDIR  "saves"

typedef struct {
	char magic[8];
	uint32_t version;
	uint32_t gb_size;
	uint32_t cart_size;
	uint32_t cart_id;
	uint32_t checksum;
} StateHeader;

static uint32_t checksum(const uint8_t *data, size_t size) {
	uint32_t h = 2166136261u;

	for (size_t i = 0; i < size; i++) {
		h ^= data[i];
		h *= 16777619u;
	}
	return h;
}

void state_path(char *out, size_t size, const char *rom_path, int slot) {
	const char *name = strrchr(rom_path, '/');
	name = name ? name + 1 : rom_path;

	size_t len = strlen(name);
	const char *dot = strrchr(name, '.');

	if (dot && dot != name) {
		len = (size_t)(dot - name);
	}

	char file[256];

	snprintf(file, sizeof file, "%.*s.state%d", (int)len, name, slot);
	if (!paths_file(PATHS_DATA, out, size, STATE_SUBDIR, file)) {
		out[0] = '\0';
	}
}

bool state_ensure_dir(void) {
	return paths_make(PATHS_DATA, STATE_SUBDIR);
}

bool state_save(const Gb *gb, const char *path) {
	if (!path[0]) {
		return false;
	}

	size_t cart_size = cartridge_state_size(gb->cart);
	size_t payload_size = sizeof *gb + cart_size;
	size_t total = sizeof(StateHeader) + payload_size;
	uint8_t *buf = malloc(total);

	if (!buf) {
		return false;
	}

	uint8_t *payload = buf + sizeof(StateHeader);

	memcpy(payload, gb, sizeof *gb);
	cartridge_state_write(gb->cart, payload + sizeof *gb);

	StateHeader h = {
		.version = STATE_VERSION,
		.gb_size = (uint32_t)sizeof *gb,
		.cart_size = (uint32_t)cart_size,
		.cart_id = cartridge_id(gb->cart),
		.checksum = checksum(payload, payload_size),
	};
	memcpy(h.magic, STATE_MAGIC, sizeof h.magic);
	memcpy(buf, &h, sizeof h);

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

static const char *validate(const Gb *gb, const StateHeader *h, const uint8_t *payload,
							size_t payload_size, size_t cart_size, Gb *scratch) {
	if (memcmp(h->magic, STATE_MAGIC, sizeof h->magic) != 0 || h->version != STATE_VERSION) {
		return "unknown format";
	}
	if (h->gb_size != sizeof *gb || h->cart_size != cart_size ||
		h->cart_id != cartridge_id(gb->cart)) {
		return "different ROM or build";
	}
	if (h->checksum != checksum(payload, payload_size)) {
		return "checksum mismatch";
	}

	memcpy(scratch, payload, sizeof *scratch);
	if (!gb_state_sane(scratch)) {
		return "invalid emulator state";
	}
	if (!cartridge_state_check(gb->cart, payload + sizeof *gb, cart_size)) {
		return "invalid cartridge state";
	}
	return NULL;
}

StateResult state_load(Gb *gb, const char *path) {
	FILE *f = fopen(path, "rb");

	if (!f) {
		return STATE_MISSING;
	}

	size_t cart_size = cartridge_state_size(gb->cart);
	size_t payload_size = sizeof *gb + cart_size;
	size_t total = sizeof(StateHeader) + payload_size;
	uint8_t *buf = malloc(total + 1);
	Gb *scratch = malloc(sizeof *scratch);

	if (!buf || !scratch) {
		fclose(f);
		free(buf);
		free(scratch);
		return STATE_INVALID;
	}

	size_t n = fread(buf, 1, total + 1, f);

	fclose(f);

	StateResult result = STATE_INVALID;
	const char *why = "size mismatch";

	if (n == total) {
		StateHeader h;
		const uint8_t *payload = buf + sizeof h;

		memcpy(&h, buf, sizeof h);
		why = validate(gb, &h, payload, payload_size, cart_size, scratch);
		if (!why) {
			Cartridge *cart = gb->cart;

			memcpy(gb, scratch, sizeof *gb);
			gb->cart = cart;
			gb->cpu.gb = gb;
			gb->step.abort = NULL;
			cartridge_state_read(cart, payload + sizeof *gb, cart_size);
			result = STATE_OK;
		}
	}

	if (result != STATE_OK) {
		verbose("State rejected: %s (%s)", why, path);
	}
	free(buf);
	free(scratch);
	return result;
}

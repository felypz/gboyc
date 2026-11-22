#include "boot.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "file_util.h"
#include "log.h"
#include "paths.h"

#define BOOT_SUBDIR "boot"

/* The DMG boot ROM starts with LD SP,$FFFE and ends with LDH ($50),A. */
static BootResult validate(const uint8_t *data, size_t size) {
	if (size != BOOT_ROM_SIZE) {
		return BOOT_ERR_SIZE;
	}
	if (data[0] != 0x31 || data[1] != 0xFE || data[2] != 0xFF) {
		return BOOT_ERR_INVALID;
	}
	if (data[BOOT_ROM_SIZE - 2] != 0xE0 || data[BOOT_ROM_SIZE - 1] != 0x50) {
		return BOOT_ERR_INVALID;
	}
	return BOOT_OK;
}

/* rom may be NULL to only validate. */
static BootResult read_boot(const char *path, uint8_t *rom) {
	FILE *f = fopen(path, "rb");

	if (!f) {
		return BOOT_ERR_OPEN;
	}

	uint8_t buf[BOOT_ROM_SIZE + 1]; /* one extra byte detects oversized files */
	size_t n = fread(buf, 1, sizeof buf, f);

	fclose(f);

	BootResult result = validate(buf, n);

	if (result == BOOT_OK && rom) {
		memcpy(rom, buf, BOOT_ROM_SIZE);
	}
	return result;
}

static bool installed_path(char *out, size_t size, const char *name) {
	return paths_file(PATHS_DATA, out, size, BOOT_SUBDIR, name);
}

bool boot_load_installed(const char *name, uint8_t rom[BOOT_ROM_SIZE]) {
	char path[PATHS_MAX];

	if (!installed_path(path, sizeof path, name)) {
		return false;
	}

	BootResult result = read_boot(path, rom);

	if (result == BOOT_OK) {
		log_info("Boot ROM: %s", path);
	} else if (result != BOOT_ERR_OPEN) {
		log_error("boot: '%s' is not a valid DMG boot ROM, ignoring", path);
	}
	return result == BOOT_OK;
}

BootResult boot_load_file(const char *path, uint8_t rom[BOOT_ROM_SIZE]) {
	BootResult result = read_boot(path, rom);

	if (result == BOOT_OK) {
		log_info("Boot ROM: %s", path);
	}
	return result;
}

bool boot_is_installed(const char *name) {
	char path[PATHS_MAX];

	return installed_path(path, sizeof path, name) && read_boot(path, NULL) == BOOT_OK;
}

/* "/x/My Boot.rom" -> "My_Boot.bin": keeps only [A-Za-z0-9_-] and forces a .bin extension. */
static void make_name(char *out, size_t size, const char *src) {
	const char *base = strrchr(src, '/');

	base = base ? base + 1 : src;

	size_t len = strlen(base);
	const char *dot = strrchr(base, '.');

	if (dot && dot != base) {
		len = (size_t)(dot - base);
	}
	if (len > size - 5) {
		len = size - 5; /* room for ".bin" and the terminator */
	}

	size_t n = 0;

	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)base[i];

		out[n++] = (isalnum(c) || c == '_' || c == '-') ? (char)c : '_';
	}
	if (n == 0) {
		memcpy(out, "boot", 4);
		n = 4;
	}
	memcpy(out + n, ".bin", 5);
}

BootResult boot_import(const char *src_path, char *name, size_t size) {
	uint8_t rom[BOOT_ROM_SIZE];
	BootResult result = read_boot(src_path, rom);

	if (result != BOOT_OK) {
		return result;
	}

	make_name(name, size, src_path);

	char dest[PATHS_MAX];

	if (!paths_make(PATHS_DATA, BOOT_SUBDIR) || !installed_path(dest, sizeof dest, name) ||
		!file_write_atomic(dest, rom, BOOT_ROM_SIZE)) {
		return BOOT_ERR_WRITE;
	}
	return BOOT_OK;
}

const char *boot_result_text(BootResult result) {
	switch (result) {
	case BOOT_OK:
		return "OK";
	case BOOT_ERR_OPEN:
		return "Cannot open file";
	case BOOT_ERR_SIZE:
		return "Must be exactly 256 bytes";
	case BOOT_ERR_INVALID:
		return "Not a DMG boot ROM";
	case BOOT_ERR_WRITE:
		return "Could not copy file";
	}
	return "Unknown error";
}

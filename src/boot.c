#include "boot.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "paths.h"

#define BOOT_SUBDIR "boot"

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

static BootResult read_boot(const char *path, uint8_t rom[BOOT_ROM_SIZE]) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return BOOT_ERR_OPEN;
	}

	uint8_t buf[BOOT_ROM_SIZE + 1];
	size_t n = fread(buf, 1, sizeof buf, f);

	fclose(f);

	BootResult result = validate(buf, n);

	if (result == BOOT_OK && rom) {
		memcpy(rom, buf, BOOT_ROM_SIZE);
	}
	return result;
}

static bool boot_path(char *out, size_t size, const char *name) {
	return paths_file(PATHS_DATA, out, size, BOOT_SUBDIR, name);
}

bool boot_load(const char *name, uint8_t rom[BOOT_ROM_SIZE]) {
	char path[PATHS_MAX];

	if (!boot_path(path, sizeof path, name)) {
		return false;
	}

	BootResult result = read_boot(path, rom);

	if (result == BOOT_OK) {
		printf("Boot ROM: %s\n", path);
	} else if (result != BOOT_ERR_OPEN) {
		fprintf(stderr, "boot: '%s' is not a valid DMG boot ROM, ignoring\n", path);
	}
	return result == BOOT_OK;
}

BootResult boot_load_file(const char *path, uint8_t rom[BOOT_ROM_SIZE]) {
	BootResult result = read_boot(path, rom);

	if (result == BOOT_OK) {
		printf("Boot ROM: %s\n", path);
	}
	return result;
}

bool boot_check(const char *name) {
	char path[PATHS_MAX];

	return boot_path(path, sizeof path, name) && read_boot(path, NULL) == BOOT_OK;
}

static void make_name(char *out, size_t size, const char *src) {
	const char *base = strrchr(src, '/');

	base = base ? base + 1 : src;

	size_t len = strlen(base);
	const char *dot = strrchr(base, '.');

	if (dot && dot != base) {
		len = (size_t)(dot - base);
	}
	if (len > size - 5) {
		len = size - 5;
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
	char tmp[PATHS_MAX + 8];

	if (!paths_make(PATHS_DATA, BOOT_SUBDIR) || !boot_path(dest, sizeof dest, name)) {
		return BOOT_ERR_WRITE;
	}
	snprintf(tmp, sizeof tmp, "%s.tmp", dest);

	FILE *f = fopen(tmp, "wb");
	bool ok = f && fwrite(rom, 1, BOOT_ROM_SIZE, f) == BOOT_ROM_SIZE;

	if (f) {
		ok = (fclose(f) == 0) && ok;
	}
	ok = ok && rename(tmp, dest) == 0;
	if (!ok) {
		remove(tmp);
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
	default:
		return "Could not copy file";
	}
}

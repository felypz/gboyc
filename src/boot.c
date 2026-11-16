#include "boot.h"

#include <stdio.h>
#include <string.h>

#define BOOT_PATH "config/boot/dmg_boot.bin"

bool boot_load(uint8_t rom[BOOT_ROM_SIZE]) {
	FILE *f = fopen(BOOT_PATH, "rb");
	if (!f) {
		return false;
	}

	uint8_t buf[BOOT_ROM_SIZE + 1];
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);

	if (n != BOOT_ROM_SIZE) {
		fprintf(stderr, "boot: '%s' must be exactly %d bytes, ignoring\n", BOOT_PATH,
				BOOT_ROM_SIZE);
		return false;
	}

	memcpy(rom, buf, BOOT_ROM_SIZE);
	printf("Boot ROM: %s\n", BOOT_PATH);
	return true;
}

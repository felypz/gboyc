#include "cartridge.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HDR_TITLE	 0x134
#define HDR_TYPE	 0x147
#define HDR_ROM_SIZE 0x148
#define HDR_RAM_SIZE 0x149
#define HDR_CHECKSUM 0x14D
#define HDR_END		 0x150

#define ROM_BANK_SIZE 0x4000
#define RAM_BANK_SIZE 0x2000
#define ROM_FILE_MAX  0x800000

typedef enum {
	MBC_NONE,
	MBC_1,
} Mbc;

struct Cartridge {
	uint8_t *rom;
	uint8_t *ram;
	size_t rom_size;
	size_t ram_size;
	Mbc mbc;
	bool ram_enabled;
	bool mode;
	uint8_t bank1;
	uint8_t bank2;
	char title[17];
};

static const size_t RAM_SIZES[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};

static bool read_file(const char *path, uint8_t **data, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cartridge: cannot open '%s'\n", path);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);

	if (len < HDR_END || len > ROM_FILE_MAX) {
		fprintf(stderr, "cartridge: invalid file size (%ld bytes)\n", len);
		fclose(f);
		return false;
	}

	*data = malloc((size_t)len);
	if (!*data || fread(*data, 1, (size_t)len, f) != (size_t)len) {
		fprintf(stderr, "cartridge: read error\n");
		free(*data);
		*data = NULL;
		fclose(f);
		return false;
	}

	fclose(f);
	*size = (size_t)len;
	return true;
}

static bool parse_header(Cartridge *c, size_t file_size) {
	uint8_t sum = 0;
	for (size_t i = HDR_TITLE; i < HDR_CHECKSUM; i++) {
		sum = (uint8_t)(sum - c->rom[i] - 1);
	}
	if (sum != c->rom[HDR_CHECKSUM]) {
		fprintf(stderr, "cartridge: header checksum mismatch\n");
		return false;
	}

	uint8_t rom_code = c->rom[HDR_ROM_SIZE];
	if (rom_code > 8) {
		fprintf(stderr, "cartridge: unsupported ROM size code 0x%02X\n", rom_code);
		return false;
	}
	c->rom_size = (size_t)0x8000 << rom_code;
	if (file_size < c->rom_size) {
		fprintf(stderr, "cartridge: file is truncated\n");
		return false;
	}

	uint8_t ram_code = c->rom[HDR_RAM_SIZE];
	if (ram_code >= sizeof RAM_SIZES / sizeof *RAM_SIZES) {
		fprintf(stderr, "cartridge: unsupported RAM size code 0x%02X\n", ram_code);
		return false;
	}
	c->ram_size = RAM_SIZES[ram_code];

	uint8_t type = c->rom[HDR_TYPE];
	switch (type) {
	case 0x00:
	case 0x08:
	case 0x09:
		c->mbc = MBC_NONE;
		c->ram_enabled = true;
		break;
	case 0x01:
	case 0x02:
	case 0x03:
		c->mbc = MBC_1;
		break;
	default:
		fprintf(stderr, "cartridge: unsupported cartridge type 0x%02X\n", type);
		return false;
	}

	for (int i = 0; i < 16; i++) {
		uint8_t ch = c->rom[HDR_TITLE + i];
		c->title[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '\0';
	}
	return true;
}

Cartridge *cartridge_load(const char *path) {
	Cartridge *c = calloc(1, sizeof *c);
	if (!c) {
		return NULL;
	}

	size_t file_size;
	if (!read_file(path, &c->rom, &file_size) || !parse_header(c, file_size)) {
		goto fail;
	}

	if (c->ram_size) {
		c->ram = calloc(1, c->ram_size);
		if (!c->ram) {
			goto fail;
		}
	}

	c->bank1 = 1;
	return c;

fail:
	cartridge_free(c);
	return NULL;
}

void cartridge_free(Cartridge *c) {
	if (!c) {
		return;
	}
	free(c->rom);
	free(c->ram);
	free(c);
}

const char *cartridge_title(const Cartridge *c) {
	return c->title;
}

uint8_t cartridge_read_rom(const Cartridge *c, uint16_t addr) {
	unsigned bank;
	if (addr < ROM_BANK_SIZE) {
		bank = c->mode ? (unsigned)c->bank2 << 5 : 0;
	} else {
		bank = ((unsigned)c->bank2 << 5) | c->bank1;
	}
	size_t offset = (size_t)bank * ROM_BANK_SIZE + (addr & (ROM_BANK_SIZE - 1));
	return c->rom[offset & (c->rom_size - 1)];
}

void cartridge_write_rom(Cartridge *c, uint16_t addr, uint8_t value) {
	if (c->mbc != MBC_1) {
		return;
	}

	switch (addr >> 13) {
	case 0:
		c->ram_enabled = (value & 0x0F) == 0x0A;
		break;
	case 1:
		c->bank1 = value & 0x1F;
		if (c->bank1 == 0) {
			c->bank1 = 1;
		}
		break;
	case 2:
		c->bank2 = value & 0x03;
		break;
	default:
		c->mode = value & 1;
		break;
	}
}

static size_t ram_offset(const Cartridge *c, uint16_t addr) {
	unsigned bank = c->mode ? c->bank2 : 0;
	return ((size_t)bank * RAM_BANK_SIZE + (addr & (RAM_BANK_SIZE - 1))) % c->ram_size;
}

uint8_t cartridge_read_ram(const Cartridge *c, uint16_t addr) {
	if (!c->ram || !c->ram_enabled) {
		return 0xFF;
	}
	return c->ram[ram_offset(c, addr)];
}

void cartridge_write_ram(Cartridge *c, uint16_t addr, uint8_t value) {
	if (c->ram && c->ram_enabled) {
		c->ram[ram_offset(c, addr)] = value;
	}
}

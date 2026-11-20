#include "cartridge.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mapper.h"
#include "verbose.h"

#define HDR_LOGO	  0x104
#define HDR_LOGO_SIZE 0x30
#define HDR_TITLE	  0x134
#define HDR_TYPE	  0x147
#define HDR_ROM_SIZE  0x148
#define HDR_RAM_SIZE  0x149
#define HDR_CHECKSUM  0x14D
#define HDR_END		  0x150

#define ROM_BANK_SIZE	0x4000
#define ROM_FILE_MAX	0x800000
#define MMM01_MENU_SIZE 0x8000
#define SAVE_PATH_MAX	4112
#define SAVE_RTC_BYTES	13
#define MBC2_RAM_SIZE	512

struct Cartridge {
	uint8_t *rom;
	uint8_t *ram;
	size_t rom_size;
	size_t ram_size;
	bool battery;
	bool dirty;
	uint32_t id;
	Mapper map;
	char title[17];
	char save_path[SAVE_PATH_MAX];
};

static const size_t RAM_SIZES[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};

static const struct {
	uint8_t type;
	MbcType mbc;
	bool battery;
	bool timer;
	bool rumble;
} TYPES[] = {
	{0x00, MBC_NONE, false, false, false},	{0x08, MBC_NONE, false, false, false},
	{0x09, MBC_NONE, true, false, false},	{0x01, MBC_1, false, false, false},
	{0x02, MBC_1, false, false, false},		{0x03, MBC_1, true, false, false},
	{0x05, MBC_2, false, false, false},		{0x06, MBC_2, true, false, false},
	{0x0B, MBC_MMM01, false, false, false}, {0x0C, MBC_MMM01, false, false, false},
	{0x0D, MBC_MMM01, true, false, false},	{0x0F, MBC_3, true, true, false},
	{0x10, MBC_3, true, true, false},		{0x11, MBC_3, false, false, false},
	{0x12, MBC_3, false, false, false},		{0x13, MBC_3, true, false, false},
	{0x19, MBC_5, false, false, false},		{0x1A, MBC_5, false, false, false},
	{0x1B, MBC_5, true, false, false},		{0x1C, MBC_5, false, false, true},
	{0x1D, MBC_5, false, false, true},		{0x1E, MBC_5, true, false, true},
	{0xFE, MBC_HUC3, true, false, false},	{0xFF, MBC_HUC1, true, false, false},
};

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

static bool checksum_ok(const uint8_t *h) {
	uint8_t sum = 0;

	for (size_t i = HDR_TITLE; i < HDR_CHECKSUM; i++) {
		sum = (uint8_t)(sum - h[i] - 1);
	}
	return sum == h[HDR_CHECKSUM];
}

static size_t find_header_base(const uint8_t *rom, size_t size) {
	if (size < 2 * MMM01_MENU_SIZE || (size & (size - 1)) != 0) {
		return 0;
	}

	size_t base = size - MMM01_MENU_SIZE;
	uint8_t type = rom[base + HDR_TYPE];

	if (type >= 0x0B && type <= 0x0D && checksum_ok(rom + base)) {
		return base;
	}
	return 0;
}

static bool detect_multicart(const Cartridge *c) {
	if (c->rom_size < 0x100000) {
		return false;
	}

	unsigned matches = 0;
	for (unsigned bank = 0x10; bank <= 0x30; bank += 0x10) {
		const uint8_t *logo = c->rom + (size_t)bank * ROM_BANK_SIZE + HDR_LOGO;
		if (memcmp(logo, c->rom + HDR_LOGO, HDR_LOGO_SIZE) == 0) {
			matches++;
		}
	}
	return matches >= 2;
}

static bool parse_header(Cartridge *c, size_t file_size) {
	size_t base = find_header_base(c->rom, file_size);
	const uint8_t *h = c->rom + base;

	if (!checksum_ok(h)) {
		fprintf(stderr, "cartridge: header checksum mismatch\n");
		return false;
	}

	if (base) {
		c->rom_size = file_size;
	} else {
		uint8_t rom_code = h[HDR_ROM_SIZE];
		if (rom_code > 8) {
			fprintf(stderr, "cartridge: unsupported ROM size code 0x%02X\n", rom_code);
			return false;
		}
		c->rom_size = (size_t)0x8000 << rom_code;
		if (file_size < c->rom_size) {
			fprintf(stderr, "cartridge: file is truncated\n");
			return false;
		}
	}

	uint8_t ram_code = h[HDR_RAM_SIZE];
	if (ram_code >= sizeof RAM_SIZES / sizeof *RAM_SIZES) {
		fprintf(stderr, "cartridge: unsupported RAM size code 0x%02X\n", ram_code);
		return false;
	}
	c->ram_size = RAM_SIZES[ram_code];

	uint8_t type = h[HDR_TYPE];
	size_t i = 0;
	while (i < sizeof TYPES / sizeof *TYPES && TYPES[i].type != type) {
		i++;
	}
	if (i == sizeof TYPES / sizeof *TYPES) {
		fprintf(stderr, "cartridge: unsupported cartridge type 0x%02X\n", type);
		return false;
	}

	c->map.type = TYPES[i].mbc;
	c->map.has_timer = TYPES[i].timer;
	c->map.rumble = TYPES[i].rumble;
	c->map.rom_size = c->rom_size;
	if (c->map.type == MBC_2) {
		c->ram_size = MBC2_RAM_SIZE;
	}
	c->battery = TYPES[i].battery && (c->ram_size || c->map.has_timer);
	c->map.multicart = c->map.type == MBC_1 && detect_multicart(c);

	for (int j = 0; j < 16; j++) {
		uint8_t ch = h[HDR_TITLE + j];
		c->title[j] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '\0';
	}
	return true;
}

static void make_save_path(Cartridge *c, const char *path) {
	snprintf(c->save_path, sizeof c->save_path, "%s", path);

	char *slash = strrchr(c->save_path, '/');
	char *dot = strrchr(c->save_path, '.');
	if (!dot || (slash && dot < slash)) {
		dot = c->save_path + strlen(c->save_path);
	}
	snprintf(dot, sizeof c->save_path - (size_t)(dot - c->save_path), ".sav");
}

static void load_save(Cartridge *c) {
	FILE *f = fopen(c->save_path, "rb");
	if (!f) {
		return;
	}

	if (c->ram_size && fread(c->ram, 1, c->ram_size, f) != c->ram_size) {
		fprintf(stderr, "cartridge: save file is too short\n");
		fclose(f);
		return;
	}

	if (c->map.has_timer) {
		uint8_t buf[SAVE_RTC_BYTES];
		if (fread(buf, 1, sizeof buf, f) == sizeof buf) {
			uint64_t t = 0;
			memcpy(c->map.rtc.regs, buf, sizeof c->map.rtc.regs);
			for (int i = 0; i < 8; i++) {
				t |= (uint64_t)buf[5 + i] << (8 * i);
			}
			c->map.rtc.last = (time_t)t;
		}
	}

	fclose(f);
	printf("Save loaded: %s\n", c->save_path);
}

bool cartridge_save(Cartridge *c) {
	if (!c || !c->battery || !c->dirty) {
		return true;
	}

	char tmp[SAVE_PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.tmp", c->save_path);

	FILE *f = fopen(tmp, "wb");
	if (!f) {
		fprintf(stderr, "cartridge: cannot write '%s'\n", tmp);
		return false;
	}

	bool ok = !c->ram_size || fwrite(c->ram, 1, c->ram_size, f) == c->ram_size;

	if (ok && c->map.has_timer) {
		uint8_t buf[SAVE_RTC_BYTES];

		rtc_update(&c->map.rtc);
		uint64_t t = (uint64_t)c->map.rtc.last;
		memcpy(buf, c->map.rtc.regs, sizeof c->map.rtc.regs);
		for (int i = 0; i < 8; i++) {
			buf[5 + i] = (uint8_t)(t >> (8 * i));
		}
		ok = fwrite(buf, 1, sizeof buf, f) == sizeof buf;
	}

	ok = (fclose(f) == 0) && ok;
	ok = ok && rename(tmp, c->save_path) == 0;

	if (!ok) {
		remove(tmp);
		fprintf(stderr, "cartridge: failed to save '%s'\n", c->save_path);
		return false;
	}

	c->dirty = false;
	verbose("Battery save written: %s", c->save_path);
	return true;
}

static uint32_t hash_rom(const uint8_t *data, size_t size) {
	uint32_t h = 2166136261u;

	for (size_t i = 0; i < size; i++) {
		h ^= data[i];
		h *= 16777619u;
	}
	return h;
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

	c->id = hash_rom(c->rom, file_size);

	if (c->ram_size) {
		c->ram = calloc(1, c->ram_size);
		if (!c->ram) {
			goto fail;
		}
	}

	mapper_reset(&c->map);

	if (c->battery) {
		make_save_path(c, path);
		load_save(c);
	}
	if (c->map.multicart) {
		printf("MBC1 multicart detected\n");
	}
	if (c->map.type == MBC_MMM01) {
		printf("MMM01 multicart detected\n");
	}
	return c;

fail:
	cartridge_free(c);
	return NULL;
}

void cartridge_free(Cartridge *c) {
	if (!c) {
		return;
	}
	cartridge_save(c);
	free(c->rom);
	free(c->ram);
	free(c);
}

const char *cartridge_title(const Cartridge *c) {
	return c->title;
}

uint8_t cartridge_read_rom(const Cartridge *c, uint16_t addr) {
	return c->rom[mapper_rom_offset(&c->map, addr) & (c->rom_size - 1)];
}

void cartridge_write_rom(Cartridge *c, uint16_t addr, uint8_t value) {
	mapper_write_rom(&c->map, addr, value);
}

uint8_t cartridge_read_ram(const Cartridge *c, uint16_t addr) {
	return mapper_read_ram(&c->map, c->ram, c->ram_size, addr);
}

void cartridge_write_ram(Cartridge *c, uint16_t addr, uint8_t value) {
	if (mapper_write_ram(&c->map, c->ram, c->ram_size, addr, value)) {
		c->dirty = true;
	}
}

uint32_t cartridge_id(const Cartridge *c) {
	return c->id;
}

size_t cartridge_state_size(const Cartridge *c) {
	return sizeof c->map + c->ram_size;
}

void cartridge_state_write(const Cartridge *c, uint8_t *out) {
	memcpy(out, &c->map, sizeof c->map);
	if (c->ram_size) {
		memcpy(out + sizeof c->map, c->ram, c->ram_size);
	}
}

bool cartridge_state_read(Cartridge *c, const uint8_t *in, size_t size) {
	if (size != cartridge_state_size(c)) {
		return false;
	}

	memcpy(&c->map, in, sizeof c->map);
	if (c->ram_size) {
		memcpy(c->ram, in + sizeof c->map, c->ram_size);
	}
	c->dirty = true;
	return true;
}

void cartridge_reset(Cartridge *c) {
	Mapper old = c->map;

	memset(&c->map, 0, sizeof c->map);
	c->map.type = old.type;
	c->map.multicart = old.multicart;
	c->map.has_timer = old.has_timer;
	c->map.rumble = old.rumble;
	c->map.rom_size = old.rom_size;
	mapper_reset(&c->map);
	c->map.rtc = old.rtc;
}

void cartridge_log_info(const Cartridge *c) {
	static const char *const MBC_NAMES[] = {
		"ROM only", "MBC1", "MBC2", "MBC3", "MBC5", "HuC1", "HuC3", "MMM01",
	};

	verbose("Cartridge: %s", c->title[0] ? c->title : "(untitled)");
	verbose("Mapper: %s%s%s%s%s", MBC_NAMES[c->map.type], c->map.multicart ? ", multicart" : "",
			c->battery ? ", battery" : "", c->map.has_timer ? ", RTC" : "",
			c->map.rumble ? ", rumble" : "");
	verbose("ROM: %zu KiB, RAM: %zu bytes", c->rom_size / 1024, c->ram_size);
}

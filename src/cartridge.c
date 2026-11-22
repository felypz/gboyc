#include "cartridge.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_util.h"
#include "hash.h"
#include "log.h"
#include "mapper.h"
#include "paths.h"

#define HDR_LOGO	  0x104
#define HDR_LOGO_SIZE 0x30
#define HDR_TITLE	  0x134
#define HDR_TITLE_LEN 16
#define HDR_TYPE	  0x147
#define HDR_ROM_SIZE  0x148
#define HDR_RAM_SIZE  0x149
#define HDR_CHECKSUM  0x14D
#define HDR_END		  0x150

#define ROM_FILE_MAX	  0x800000
#define ROM_BASE_SIZE	  0x8000 /* ROM size code 0 */
#define ROM_SIZE_CODE_MAX 8

/* MBC1 multicarts (e.g. 4-in-1 compilations) repeat the Nintendo logo at the start of each game. */
#define MULTICART_MIN_SIZE		0x100000
#define MULTICART_GAME_STRIDE	0x10 /* in ROM banks */
#define MULTICART_LAST_GAME		0x30
#define MULTICART_MIN_LOGO_HITS 2

#define MMM01_TYPE_FIRST 0x0B
#define MMM01_TYPE_LAST	 0x0D

struct Cartridge {
	uint8_t *rom;
	uint8_t *ram;
	size_t rom_size;
	size_t ram_size;
	bool battery;
	bool dirty; /* battery RAM / RTC changed since the last flush */
	uint32_t id;
	Mapper map;
	char title[HDR_TITLE_LEN + 1];
	char save_path[PATHS_MAX];
};

static const size_t RAM_SIZES[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};

static const char *const MBC_NAMES[MBC_COUNT] = {
	[MBC_NONE] = "ROM only", [MBC_1] = "MBC1",	  [MBC_2] = "MBC2",	   [MBC_3] = "MBC3",
	[MBC_5] = "MBC5",		 [MBC_HUC1] = "HuC1", [MBC_HUC3] = "HuC3", [MBC_MMM01] = "MMM01",
};

/* Header byte 0x147 -> mapper and features. */
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

#define TYPES_COUNT (sizeof TYPES / sizeof *TYPES)

static bool read_file(const char *path, uint8_t **data, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		log_error("cartridge: cannot open '%s'", path);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);

	if (len < HDR_END || len > ROM_FILE_MAX) {
		log_error("cartridge: invalid file size (%ld bytes)", len);
		fclose(f);
		return false;
	}

	*data = malloc((size_t)len);
	if (!*data || fread(*data, 1, (size_t)len, f) != (size_t)len) {
		log_error("cartridge: read error");
		free(*data);
		*data = NULL;
		fclose(f);
		return false;
	}

	fclose(f);
	*size = (size_t)len;
	return true;
}

static bool header_checksum_ok(const uint8_t *header_base) {
	uint8_t sum = 0;

	for (size_t i = HDR_TITLE; i < HDR_CHECKSUM; i++) {
		sum = (uint8_t)(sum - header_base[i] - 1);
	}
	return sum == header_base[HDR_CHECKSUM];
}

/*
 * MMM01 carts keep their real header in the last 32 KiB (the boot menu), not at
 * the start of the file. Returns that offset, or 0 for every other cartridge.
 */
static size_t find_header_base(const uint8_t *rom, size_t size) {
	if (size < 2 * MAPPER_MMM01_MENU_SIZE || (size & (size - 1)) != 0) {
		return 0;
	}

	size_t base = size - MAPPER_MMM01_MENU_SIZE;
	uint8_t type = rom[base + HDR_TYPE];

	if (type >= MMM01_TYPE_FIRST && type <= MMM01_TYPE_LAST && header_checksum_ok(rom + base)) {
		return base;
	}
	return 0;
}

static bool detect_multicart(const Cartridge *c) {
	if (c->rom_size < MULTICART_MIN_SIZE) {
		return false;
	}

	unsigned hits = 0;
	for (unsigned bank = MULTICART_GAME_STRIDE; bank <= MULTICART_LAST_GAME;
		 bank += MULTICART_GAME_STRIDE) {
		const uint8_t *logo = c->rom + (size_t)bank * MAPPER_ROM_BANK_SIZE + HDR_LOGO;
		if (memcmp(logo, c->rom + HDR_LOGO, HDR_LOGO_SIZE) == 0) {
			hits++;
		}
	}
	return hits >= MULTICART_MIN_LOGO_HITS;
}

static bool parse_header(Cartridge *c, size_t file_size) {
	size_t base = find_header_base(c->rom, file_size);
	const uint8_t *h = c->rom + base;

	if (!header_checksum_ok(h)) {
		log_error("cartridge: header checksum mismatch");
		return false;
	}

	if (base) {
		c->rom_size = file_size;
	} else {
		uint8_t rom_code = h[HDR_ROM_SIZE];
		if (rom_code > ROM_SIZE_CODE_MAX) {
			log_error("cartridge: unsupported ROM size code 0x%02X", rom_code);
			return false;
		}
		c->rom_size = (size_t)ROM_BASE_SIZE << rom_code;
		if (file_size < c->rom_size) {
			log_error("cartridge: file is truncated");
			return false;
		}
	}

	uint8_t ram_code = h[HDR_RAM_SIZE];
	if (ram_code >= sizeof RAM_SIZES / sizeof *RAM_SIZES) {
		log_error("cartridge: unsupported RAM size code 0x%02X", ram_code);
		return false;
	}
	c->ram_size = RAM_SIZES[ram_code];

	uint8_t type = h[HDR_TYPE];
	size_t i = 0;
	while (i < TYPES_COUNT && TYPES[i].type != type) {
		i++;
	}
	if (i == TYPES_COUNT) {
		log_error("cartridge: unsupported cartridge type 0x%02X", type);
		return false;
	}

	c->map.type = TYPES[i].mbc;
	c->map.has_timer = TYPES[i].timer;
	c->map.rumble = TYPES[i].rumble;
	c->map.rom_size = c->rom_size;
	if (c->map.type == MBC_2) {
		c->ram_size = MAPPER_MBC2_RAM_SIZE; /* header says 0; the RAM is inside the chip */
	}
	c->battery = TYPES[i].battery && (c->ram_size || c->map.has_timer);
	c->map.multicart = c->map.type == MBC_1 && detect_multicart(c);

	for (int j = 0; j < HDR_TITLE_LEN; j++) {
		uint8_t ch = h[HDR_TITLE + j];
		c->title[j] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '\0';
	}
	return true;
}

/* "<rom path without extension>.sav", next to the ROM. */
static bool make_save_path(Cartridge *c, const char *rom_path) {
	int n = snprintf(c->save_path, sizeof c->save_path, "%s", rom_path);

	if (n < 0 || (size_t)n >= sizeof c->save_path) {
		return false;
	}

	char *slash = strrchr(c->save_path, '/');
	char *dot = strrchr(c->save_path, '.');
	if (!dot || (slash && dot < slash)) {
		dot = c->save_path + strlen(c->save_path);
	}

	n = snprintf(dot, sizeof c->save_path - (size_t)(dot - c->save_path), ".sav");
	return n >= 0 && (size_t)n < sizeof c->save_path - (size_t)(dot - c->save_path);
}

static void load_battery(Cartridge *c) {
	FILE *f = fopen(c->save_path, "rb");
	if (!f) {
		return;
	}

	if (c->ram_size && fread(c->ram, 1, c->ram_size, f) != c->ram_size) {
		log_error("cartridge: save file is too short");
		fclose(f);
		return;
	}

	if (c->map.has_timer) {
		uint8_t buf[RTC_SAVE_SIZE];

		/* An older save without clock data is fine: the RTC just starts from now. */
		if (fread(buf, 1, sizeof buf, f) == sizeof buf) {
			rtc_deserialize(&c->map.rtc, buf);
		}
	}

	fclose(f);
	log_info("Save loaded: %s", c->save_path);
}

bool cartridge_flush_battery(Cartridge *c) {
	if (!c || !c->battery || !c->dirty) {
		return true;
	}

	AtomicFile af;

	if (!atomic_file_open(&af, c->save_path, "wb")) {
		log_error("cartridge: cannot write '%s.tmp'", c->save_path);
		return false;
	}

	bool ok = !c->ram_size || fwrite(c->ram, 1, c->ram_size, af.file) == c->ram_size;

	if (ok && c->map.has_timer) {
		uint8_t buf[RTC_SAVE_SIZE];

		rtc_update(&c->map.rtc);
		rtc_serialize(&c->map.rtc, buf);
		ok = fwrite(buf, 1, sizeof buf, af.file) == sizeof buf;
	}

	if (!ok) {
		atomic_file_abort(&af);
	} else {
		ok = atomic_file_commit(&af);
	}
	if (!ok) {
		log_error("cartridge: failed to save '%s'", c->save_path);
		return false;
	}

	c->dirty = false;
	log_verbose("Battery save written: %s", c->save_path);
	return true;
}

void cartridge_discard_battery_changes(Cartridge *c) {
	if (c) {
		c->dirty = false;
	}
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

	c->id = fnv1a32(c->rom, file_size);

	if (c->ram_size) {
		c->ram = calloc(1, c->ram_size);
		if (!c->ram) {
			goto fail;
		}
	}

	mapper_reset(&c->map);

	if (c->battery) {
		if (!make_save_path(c, path)) {
			log_error("cartridge: path too long for the save file");
			goto fail;
		}
		load_battery(c);
	}
	if (c->map.multicart) {
		log_info("MBC1 multicart detected");
	}
	if (c->map.type == MBC_MMM01) {
		log_info("MMM01 multicart detected");
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
	cartridge_flush_battery(c);
	free(c->rom);
	free(c->ram);
	free(c);
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
	c->map.rtc = old.rtc; /* the clock keeps running across a reset */
}

const char *cartridge_title(const Cartridge *c) {
	return c->title;
}

void cartridge_log_info(const Cartridge *c) {
	log_verbose("Cartridge: %s", c->title[0] ? c->title : "(untitled)");
	log_verbose("Mapper: %s%s%s%s%s", MBC_NAMES[c->map.type], c->map.multicart ? ", multicart" : "",
				c->battery ? ", battery" : "", c->map.has_timer ? ", RTC" : "",
				c->map.rumble ? ", rumble" : "");
	log_verbose("ROM: %zu KiB, RAM: %zu bytes", c->rom_size / 1024, c->ram_size);
}

uint8_t cartridge_read_rom(const Cartridge *c, uint16_t addr) {
	return c->rom[mapper_rom_offset(&c->map, addr) & (c->rom_size - 1)];
}

void cartridge_write_rom(Cartridge *c, uint16_t addr, uint8_t value) {
	mapper_write_rom(&c->map, addr, value);
}

/* Bank currently mapped at 0x4000-0x7FFF (for crash reports). */
unsigned cartridge_rom_bank(const Cartridge *c) {
	size_t offset = mapper_rom_offset(&c->map, MAPPER_ROM_BANK_SIZE);

	return (unsigned)((offset / MAPPER_ROM_BANK_SIZE) % (c->rom_size / MAPPER_ROM_BANK_SIZE));
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

/* The saved mapper must describe the same cartridge (type, features, ROM size). */
bool cartridge_state_check(const Cartridge *c, const uint8_t *in, size_t size) {
	if (size != cartridge_state_size(c)) {
		return false;
	}

	Mapper m;

	memcpy(&m, in, sizeof m);
	return m.type == c->map.type && m.multicart == c->map.multicart &&
		   m.has_timer == c->map.has_timer && m.rumble == c->map.rumble &&
		   m.rom_size == c->map.rom_size;
}

bool cartridge_state_restore(Cartridge *c, const uint8_t *in, size_t size) {
	if (size != cartridge_state_size(c)) {
		return false;
	}

	memcpy(&c->map, in, sizeof c->map);
	if (c->ram_size) {
		memcpy(c->ram, in + sizeof c->map, c->ram_size);
	}
	return true;
}

bool cartridge_state_load(Cartridge *c, const uint8_t *in, size_t size) {
	if (!cartridge_state_check(c, in, size) || !cartridge_state_restore(c, in, size)) {
		return false;
	}
	c->dirty = true;
	return true;
}

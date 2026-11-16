#include "cartridge.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HDR_TITLE	 0x134
#define HDR_TYPE	 0x147
#define HDR_ROM_SIZE 0x148
#define HDR_RAM_SIZE 0x149
#define HDR_CHECKSUM 0x14D
#define HDR_END		 0x150

#define ROM_BANK_SIZE  0x4000
#define RAM_BANK_SIZE  0x2000
#define ROM_FILE_MAX   0x800000
#define SAVE_PATH_MAX  4112
#define SAVE_RTC_BYTES 13

#define RTC_HALT  0x40
#define RTC_CARRY 0x80

typedef enum {
	MBC_NONE,
	MBC_1,
	MBC_3,
	MBC_5,
} Mbc;

typedef struct {
	uint8_t regs[5];
	uint8_t latched[5];
	uint8_t latch;
	time_t last;
} Rtc;

struct Cartridge {
	uint8_t *rom;
	uint8_t *ram;
	size_t rom_size;
	size_t ram_size;
	Mbc mbc;
	bool battery;
	bool timer;
	bool rumble;
	bool dirty;
	bool ram_enabled;
	bool mode;
	uint8_t bank1;
	uint8_t bank2;
	unsigned rom_bank;
	unsigned ram_bank;
	Rtc rtc;
	char title[17];
	char save_path[SAVE_PATH_MAX];
};

static const size_t RAM_SIZES[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};

static const struct {
	uint8_t type;
	Mbc mbc;
	bool battery;
	bool timer;
	bool rumble;
} TYPES[] = {
	{0x00, MBC_NONE, false, false, false}, {0x08, MBC_NONE, false, false, false},
	{0x09, MBC_NONE, true, false, false},  {0x01, MBC_1, false, false, false},
	{0x02, MBC_1, false, false, false},	   {0x03, MBC_1, true, false, false},
	{0x0F, MBC_3, true, true, false},	   {0x10, MBC_3, true, true, false},
	{0x11, MBC_3, false, false, false},	   {0x12, MBC_3, false, false, false},
	{0x13, MBC_3, true, false, false},	   {0x19, MBC_5, false, false, false},
	{0x1A, MBC_5, false, false, false},	   {0x1B, MBC_5, true, false, false},
	{0x1C, MBC_5, false, false, true},	   {0x1D, MBC_5, false, false, true},
	{0x1E, MBC_5, true, false, true},
};

static void rtc_update(Rtc *r) {
	time_t now = time(NULL);

	if ((r->regs[4] & RTC_HALT) || now <= r->last) {
		r->last = now;
		return;
	}

	unsigned days = ((unsigned)(r->regs[4] & 1) << 8) | r->regs[3];
	uint64_t total = r->regs[0] + r->regs[1] * 60ULL + r->regs[2] * 3600ULL + days * 86400ULL +
					 (uint64_t)(now - r->last);
	r->last = now;

	r->regs[0] = (uint8_t)(total % 60);
	total /= 60;
	r->regs[1] = (uint8_t)(total % 60);
	total /= 60;
	r->regs[2] = (uint8_t)(total % 24);
	total /= 24;

	uint8_t flags = r->regs[4] & (RTC_HALT | RTC_CARRY);
	if (total >= 512) {
		flags |= RTC_CARRY;
		total %= 512;
	}
	r->regs[3] = (uint8_t)(total & 0xFF);
	r->regs[4] = flags | (uint8_t)(total >> 8);
}

static void rtc_write(Rtc *r, unsigned reg, uint8_t value) {
	static const uint8_t masks[5] = {0x3F, 0x3F, 0x1F, 0xFF, 0xC1};

	rtc_update(r);
	r->regs[reg] = value & masks[reg];
}

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
	size_t i = 0;
	while (i < sizeof TYPES / sizeof *TYPES && TYPES[i].type != type) {
		i++;
	}
	if (i == sizeof TYPES / sizeof *TYPES) {
		fprintf(stderr, "cartridge: unsupported cartridge type 0x%02X\n", type);
		return false;
	}

	c->mbc = TYPES[i].mbc;
	c->timer = TYPES[i].timer;
	c->rumble = TYPES[i].rumble;
	c->battery = TYPES[i].battery && (c->ram_size || c->timer);
	c->ram_enabled = (c->mbc == MBC_NONE);

	for (int j = 0; j < 16; j++) {
		uint8_t ch = c->rom[HDR_TITLE + j];
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

	if (c->timer) {
		uint8_t buf[SAVE_RTC_BYTES];
		if (fread(buf, 1, sizeof buf, f) == sizeof buf) {
			uint64_t t = 0;
			memcpy(c->rtc.regs, buf, sizeof c->rtc.regs);
			for (int i = 0; i < 8; i++) {
				t |= (uint64_t)buf[5 + i] << (8 * i);
			}
			c->rtc.last = (time_t)t;
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

	if (ok && c->timer) {
		uint8_t buf[SAVE_RTC_BYTES];
		uint64_t t = (uint64_t)c->rtc.last;

		rtc_update(&c->rtc);
		memcpy(buf, c->rtc.regs, sizeof c->rtc.regs);
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
	c->rom_bank = 1;
	c->rtc.last = time(NULL);

	if (c->battery) {
		make_save_path(c, path);
		load_save(c);
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

static unsigned rom_bank_for(const Cartridge *c, uint16_t addr) {
	switch (c->mbc) {
	case MBC_1:
		if (addr < ROM_BANK_SIZE) {
			return c->mode ? (unsigned)c->bank2 << 5 : 0;
		}
		return ((unsigned)c->bank2 << 5) | c->bank1;
	case MBC_3:
	case MBC_5:
		return addr < ROM_BANK_SIZE ? 0 : c->rom_bank;
	default:
		return addr >> 14;
	}
}

uint8_t cartridge_read_rom(const Cartridge *c, uint16_t addr) {
	size_t offset = (size_t)rom_bank_for(c, addr) * ROM_BANK_SIZE + (addr & (ROM_BANK_SIZE - 1));
	return c->rom[offset & (c->rom_size - 1)];
}

static void mbc1_write(Cartridge *c, uint16_t addr, uint8_t value) {
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

static void mbc3_write(Cartridge *c, uint16_t addr, uint8_t value) {
	switch (addr >> 13) {
	case 0:
		c->ram_enabled = (value & 0x0F) == 0x0A;
		break;
	case 1:
		c->rom_bank = value & 0x7F;
		if (c->rom_bank == 0) {
			c->rom_bank = 1;
		}
		break;
	case 2:
		c->ram_bank = value & 0x0F;
		break;
	default:
		if (c->timer && c->rtc.latch == 0 && value == 1) {
			rtc_update(&c->rtc);
			memcpy(c->rtc.latched, c->rtc.regs, sizeof c->rtc.latched);
		}
		c->rtc.latch = value;
		break;
	}
}

static void mbc5_write(Cartridge *c, uint16_t addr, uint8_t value) {
	switch (addr >> 12) {
	case 0:
	case 1:
		c->ram_enabled = (value & 0x0F) == 0x0A;
		break;
	case 2:
		c->rom_bank = (c->rom_bank & 0x100) | value;
		break;
	case 3:
		c->rom_bank = (c->rom_bank & 0xFF) | ((unsigned)(value & 1) << 8);
		break;
	case 4:
	case 5:
		c->ram_bank = value & (c->rumble ? 0x07 : 0x0F);
		break;
	default:
		break;
	}
}

void cartridge_write_rom(Cartridge *c, uint16_t addr, uint8_t value) {
	switch (c->mbc) {
	case MBC_1:
		mbc1_write(c, addr, value);
		break;
	case MBC_3:
		mbc3_write(c, addr, value);
		break;
	case MBC_5:
		mbc5_write(c, addr, value);
		break;
	default:
		break;
	}
}

static size_t ram_offset(const Cartridge *c, uint16_t addr) {
	unsigned bank = 0;

	if (c->mbc == MBC_1) {
		bank = c->mode ? c->bank2 : 0;
	} else if (c->mbc == MBC_3 || c->mbc == MBC_5) {
		bank = c->ram_bank;
	}
	return ((size_t)bank * RAM_BANK_SIZE + (addr & (RAM_BANK_SIZE - 1))) % c->ram_size;
}

static bool rtc_selected(const Cartridge *c) {
	return c->mbc == MBC_3 && c->ram_bank >= 8;
}

static bool ram_selected(const Cartridge *c) {
	return c->ram && !(c->mbc == MBC_3 && c->ram_bank > 3);
}

uint8_t cartridge_read_ram(const Cartridge *c, uint16_t addr) {
	if (!c->ram_enabled) {
		return 0xFF;
	}
	if (rtc_selected(c)) {
		return (c->timer && c->ram_bank <= 0x0C) ? c->rtc.latched[c->ram_bank - 8] : 0xFF;
	}
	if (!ram_selected(c)) {
		return 0xFF;
	}
	return c->ram[ram_offset(c, addr)];
}

void cartridge_write_ram(Cartridge *c, uint16_t addr, uint8_t value) {
	if (!c->ram_enabled) {
		return;
	}
	if (rtc_selected(c)) {
		if (c->timer && c->ram_bank <= 0x0C) {
			rtc_write(&c->rtc, c->ram_bank - 8, value);
			c->dirty = true;
		}
		return;
	}
	if (ram_selected(c)) {
		c->ram[ram_offset(c, addr)] = value;
		c->dirty = true;
	}
}

#include "mapper.h"

#include <string.h>

#define RAM_BANK_SIZE 0x2000

#define RTC_HALT	 0x40
#define RTC_CARRY	 0x80
#define RTC_DAY_BITS 512 /* 9-bit day counter */

#define HUC_MODE_RAM_RO 0x00
#define HUC_MODE_RAM_RW 0x0A
#define HUC_MODE_IR		0x0E

#define MBC3_RAM_BANKS 4 /* ram_bank 0-3 select RAM, 8-0C select an RTC register */
#define MBC3_RTC_FIRST 0x08
#define MBC3_RTC_LAST  0x0C

#define MBC1_ROM_BANK_MASK		  0x1F
#define MBC1_MULTICART_BANK_MASK  0x0F
#define MBC1_ROM_BANK_SHIFT		  5
#define MBC1_MULTICART_BANK_SHIFT 4

/* Cartridge RAM enable: the low nibble must be 0xA. */
static bool ram_gate(uint8_t value) {
	return (value & 0x0F) == 0x0A;
}

/* Bank registers can't select bank 0 for the switchable window; it maps to bank 1. */
static unsigned nonzero_bank(unsigned bank) {
	return bank ? bank : 1;
}

void rtc_update(Rtc *r) {
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
	if (total >= RTC_DAY_BITS) {
		flags |= RTC_CARRY;
		total %= RTC_DAY_BITS;
	}
	r->regs[3] = (uint8_t)(total & 0xFF);
	r->regs[4] = flags | (uint8_t)(total >> 8);
}

static void rtc_write(Rtc *r, unsigned reg, uint8_t value) {
	static const uint8_t masks[RTC_REG_COUNT] = {0x3F, 0x3F, 0x1F, 0xFF, 0xC1};

	rtc_update(r);
	r->regs[reg] = value & masks[reg];
}

void rtc_serialize(const Rtc *r, uint8_t *out) {
	uint64_t last = (uint64_t)r->last;

	memcpy(out, r->regs, RTC_REG_COUNT);
	for (int i = 0; i < 8; i++) {
		out[RTC_REG_COUNT + i] = (uint8_t)(last >> (8 * i));
	}
}

void rtc_deserialize(Rtc *r, const uint8_t *in) {
	uint64_t last = 0;

	memcpy(r->regs, in, RTC_REG_COUNT);
	for (int i = 0; i < 8; i++) {
		last |= (uint64_t)in[RTC_REG_COUNT + i] << (8 * i);
	}
	r->last = (time_t)last;
}

void mapper_reset(Mapper *m) {
	m->ram_enabled = (m->type == MBC_NONE);
	m->mbc1_bank_low = 1;
	m->rom_bank = 1;
	m->rtc.last = time(NULL);
}

static size_t mmm01_rom_offset(const Mapper *m, uint16_t addr) {
	const Mmm01 *c = &m->mmm;

	if (!c->mapped) {
		return m->rom_size - MAPPER_MMM01_MENU_SIZE + (addr & (MAPPER_MMM01_MENU_SIZE - 1));
	}

	unsigned low;
	unsigned mid;

	if (addr < MAPPER_ROM_BANK_SIZE) {
		low = c->rom_low & c->rom_mask;
		if (c->multiplex) {
			mid = m->mode ? c->ram_low : (c->ram_low & c->ram_mask);
		} else {
			mid = c->rom_mid;
		}
	} else {
		low = c->rom_low;
		if ((low & ~c->rom_mask & 0x1F) == 0) {
			low |= 1;
		}
		mid = c->multiplex ? c->ram_low : c->rom_mid;
	}

	unsigned bank = ((unsigned)c->rom_high << 7) | (mid << 5) | low;
	return (size_t)bank * MAPPER_ROM_BANK_SIZE + (addr & (MAPPER_ROM_BANK_SIZE - 1));
}

static unsigned mmm01_ram_bank(const Mapper *m) {
	const Mmm01 *c = &m->mmm;
	unsigned low;

	if (c->multiplex) {
		low = c->rom_mid;
	} else {
		low = m->mode ? c->ram_low : (c->ram_low & c->ram_mask);
	}
	return ((unsigned)c->ram_high << 2) | low;
}

size_t mapper_rom_offset(const Mapper *m, uint16_t addr) {
	unsigned bank;

	switch (m->type) {
	case MBC_1: {
		/* Multicarts wire only 4 bits of the low register, so the high one sits at bit 4. */
		unsigned shift = m->multicart ? MBC1_MULTICART_BANK_SHIFT : MBC1_ROM_BANK_SHIFT;
		unsigned mask = m->multicart ? MBC1_MULTICART_BANK_MASK : MBC1_ROM_BANK_MASK;
		unsigned high = (unsigned)m->mbc1_bank_high << shift;

		if (addr < MAPPER_ROM_BANK_SIZE) {
			bank = m->mode ? high : 0;
		} else {
			bank = high | (m->mbc1_bank_low & mask);
		}
		break;
	}
	case MBC_MMM01:
		return mmm01_rom_offset(m, addr);
	case MBC_NONE:
		bank = addr / MAPPER_ROM_BANK_SIZE;
		break;
	default:
		bank = addr < MAPPER_ROM_BANK_SIZE ? 0 : m->rom_bank;
		break;
	}
	return (size_t)bank * MAPPER_ROM_BANK_SIZE + (addr & (MAPPER_ROM_BANK_SIZE - 1));
}

static void mbc1_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 13) {
	case 0:
		m->ram_enabled = ram_gate(value);
		break;
	case 1:
		m->mbc1_bank_low = (uint8_t)nonzero_bank(value & MBC1_ROM_BANK_MASK);
		break;
	case 2:
		m->mbc1_bank_high = value & 0x03;
		break;
	default:
		m->mode = value & 1;
		break;
	}
}

static void mbc2_write(Mapper *m, uint16_t addr, uint8_t value) {
	if (addr >= MAPPER_ROM_BANK_SIZE) {
		return;
	}
	/* Address bit 8 chooses between the ROM bank and RAM enable registers. */
	if (addr & 0x100) {
		m->rom_bank = nonzero_bank(value & 0x0F);
	} else {
		m->ram_enabled = ram_gate(value);
	}
}

static void mbc3_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 13) {
	case 0:
		m->ram_enabled = ram_gate(value);
		break;
	case 1:
		m->rom_bank = nonzero_bank(value & 0x7F);
		break;
	case 2:
		m->ram_bank = value & 0x0F;
		break;
	default:
		/* Writing 0 then 1 latches the clock into the readable registers. */
		if (m->has_timer && m->rtc.latch == 0 && value == 1) {
			rtc_update(&m->rtc);
			memcpy(m->rtc.latched, m->rtc.regs, RTC_REG_COUNT);
		}
		m->rtc.latch = value;
		break;
	}
}

static void mbc5_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 12) {
	case 0:
	case 1:
		m->ram_enabled = ram_gate(value);
		break;
	case 2:
		m->rom_bank = (m->rom_bank & 0x100) | value;
		break;
	case 3:
		m->rom_bank = (m->rom_bank & 0xFF) | ((unsigned)(value & 1) << 8);
		break;
	case 4:
	case 5:
		m->ram_bank = value & (m->rumble ? 0x07 : 0x0F); /* rumble motor uses bit 3 */
		break;
	default:
		break;
	}
}

static void huc_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 13) {
	case 0:
		m->huc_mode = value & 0x0F;
		break;
	case 1:
		m->rom_bank = value & (m->type == MBC_HUC3 ? 0x7F : 0x3F);
		break;
	case 2:
		m->ram_bank = value & 0x03;
		break;
	default:
		break;
	}
}

static void mmm01_write(Mapper *m, uint16_t addr, uint8_t value) {
	Mmm01 *c = &m->mmm;

	switch (addr >> 13) {
	case 0:
		m->ram_enabled = ram_gate(value);
		if (!c->mapped) {
			c->ram_mask = (value >> 4) & 3;
			c->mapped = value & 0x40;
		}
		break;
	case 1:
		c->rom_low = (uint8_t)((c->rom_low & c->rom_mask) | ((value & 0x1F) & ~c->rom_mask));
		if (!c->mapped) {
			c->rom_mid = (value >> 5) & 3;
		}
		break;
	case 2:
		c->ram_low = (uint8_t)((c->ram_low & c->ram_mask) | ((value & 3) & ~c->ram_mask));
		if (!c->mapped) {
			c->ram_high = (value >> 2) & 3;
			c->rom_high = (value >> 4) & 3;
			c->mode_lock = value & 0x40;
		}
		break;
	default:
		if (!c->mapped) {
			c->rom_mask = (value >> 1) & 0x1E;
			c->multiplex = value & 0x40;
		}
		if (!c->mode_lock) {
			m->mode = value & 1;
		}
		break;
	}
}

void mapper_write_rom(Mapper *m, uint16_t addr, uint8_t value) {
	switch (m->type) {
	case MBC_1:
		mbc1_write(m, addr, value);
		break;
	case MBC_2:
		mbc2_write(m, addr, value);
		break;
	case MBC_3:
		mbc3_write(m, addr, value);
		break;
	case MBC_5:
		mbc5_write(m, addr, value);
		break;
	case MBC_HUC1:
	case MBC_HUC3:
		huc_write(m, addr, value);
		break;
	case MBC_MMM01:
		mmm01_write(m, addr, value);
		break;
	default:
		break;
	}
}

static size_t ram_offset(const Mapper *m, size_t ram_size, uint16_t addr) {
	unsigned bank = 0;

	switch (m->type) {
	case MBC_1:
		bank = m->mode ? m->mbc1_bank_high : 0;
		break;
	case MBC_3:
	case MBC_5:
	case MBC_HUC1:
	case MBC_HUC3:
		bank = m->ram_bank;
		break;
	case MBC_MMM01:
		bank = mmm01_ram_bank(m);
		break;
	default:
		break;
	}
	return ((size_t)bank * RAM_BANK_SIZE + (addr & (RAM_BANK_SIZE - 1))) % ram_size;
}

/* Whether the RAM window reaches real RAM (MBC2 and the MBC3 clock are handled separately). */
static bool ram_access_allowed(const Mapper *m, bool is_write) {
	switch (m->type) {
	case MBC_3:
		return m->ram_enabled && m->ram_bank < MBC3_RAM_BANKS;
	case MBC_HUC1:
		return m->huc_mode != HUC_MODE_IR;
	case MBC_HUC3:
		/* Reads also work in mode 0 (read-only); only mode A enables writes. */
		return m->huc_mode == HUC_MODE_RAM_RW || (!is_write && m->huc_mode == HUC_MODE_RAM_RO);
	default:
		return m->ram_enabled;
	}
}

static bool mbc3_rtc_selected(const Mapper *m) {
	return m->ram_enabled && m->ram_bank >= MBC3_RTC_FIRST;
}

uint8_t mapper_read_ram(const Mapper *m, const uint8_t *ram, size_t ram_size, uint16_t addr) {
	switch (m->type) {
	case MBC_2:
		return m->ram_enabled ? (uint8_t)(ram[addr & (MAPPER_MBC2_RAM_SIZE - 1)] | 0xF0) : 0xFF;
	case MBC_3:
		if (mbc3_rtc_selected(m)) {
			bool valid = m->has_timer && m->ram_bank <= MBC3_RTC_LAST;
			return valid ? m->rtc.latched[m->ram_bank - MBC3_RTC_FIRST] : 0xFF;
		}
		break;
	case MBC_HUC1:
		if (m->huc_mode == HUC_MODE_IR) {
			return 0xC0; /* no IR light seen */
		}
		break;
	case MBC_HUC3:
		if (m->huc_mode >= 0x0B && m->huc_mode <= HUC_MODE_IR) {
			return 0x01;
		}
		break;
	default:
		break;
	}

	if (!ram || !ram_access_allowed(m, false)) {
		return 0xFF;
	}
	return ram[ram_offset(m, ram_size, addr)];
}

bool mapper_write_ram(Mapper *m, uint8_t *ram, size_t ram_size, uint16_t addr, uint8_t value) {
	if (m->type == MBC_2) {
		if (!m->ram_enabled) {
			return false;
		}
		ram[addr & (MAPPER_MBC2_RAM_SIZE - 1)] = value & 0x0F;
		return true;
	}

	if (m->type == MBC_3 && mbc3_rtc_selected(m)) {
		if (!m->has_timer || m->ram_bank > MBC3_RTC_LAST) {
			return false;
		}
		rtc_write(&m->rtc, m->ram_bank - MBC3_RTC_FIRST, value);
		return true;
	}

	if (!ram || !ram_access_allowed(m, true)) {
		return false;
	}
	ram[ram_offset(m, ram_size, addr)] = value;
	return true;
}

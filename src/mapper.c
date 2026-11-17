#include "mapper.h"

#define ROM_BANK_SIZE	0x4000
#define RAM_BANK_SIZE	0x2000
#define MMM01_MENU_SIZE 0x8000

#define RTC_HALT  0x40
#define RTC_CARRY 0x80

#define HUC_IR 0x0E

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

void mapper_reset(Mapper *m) {
	m->ram_enabled = (m->type == MBC_NONE);
	m->bank1 = 1;
	m->rom_bank = 1;
	m->rtc.last = time(NULL);
}

static size_t mmm01_rom_offset(const Mapper *m, uint16_t addr) {
	const Mmm01 *c = &m->mmm;

	if (!c->mapped) {
		return m->rom_size - MMM01_MENU_SIZE + (addr & 0x7FFF);
	}

	unsigned low;
	unsigned mid;

	if (addr < ROM_BANK_SIZE) {
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
	return (size_t)bank * ROM_BANK_SIZE + (addr & (ROM_BANK_SIZE - 1));
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
		unsigned shift = m->multicart ? 4 : 5;
		unsigned mask = m->multicart ? 0x0F : 0x1F;

		if (addr < ROM_BANK_SIZE) {
			bank = m->mode ? (unsigned)m->bank2 << shift : 0;
		} else {
			bank = ((unsigned)m->bank2 << shift) | (m->bank1 & mask);
		}
		break;
	}
	case MBC_MMM01:
		return mmm01_rom_offset(m, addr);
	case MBC_NONE:
		bank = addr >> 14;
		break;
	default:
		bank = addr < ROM_BANK_SIZE ? 0 : m->rom_bank;
		break;
	}
	return (size_t)bank * ROM_BANK_SIZE + (addr & (ROM_BANK_SIZE - 1));
}

static void mbc1_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 13) {
	case 0:
		m->ram_enabled = (value & 0x0F) == 0x0A;
		break;
	case 1:
		m->bank1 = value & 0x1F;
		if (m->bank1 == 0) {
			m->bank1 = 1;
		}
		break;
	case 2:
		m->bank2 = value & 0x03;
		break;
	default:
		m->mode = value & 1;
		break;
	}
}

static void mbc2_write(Mapper *m, uint16_t addr, uint8_t value) {
	if (addr >= 0x4000) {
		return;
	}
	if (addr & 0x100) {
		m->rom_bank = value & 0x0F;
		if (m->rom_bank == 0) {
			m->rom_bank = 1;
		}
	} else {
		m->ram_enabled = (value & 0x0F) == 0x0A;
	}
}

static void mbc3_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 13) {
	case 0:
		m->ram_enabled = (value & 0x0F) == 0x0A;
		break;
	case 1:
		m->rom_bank = value & 0x7F;
		if (m->rom_bank == 0) {
			m->rom_bank = 1;
		}
		break;
	case 2:
		m->ram_bank = value & 0x0F;
		break;
	default:
		if (m->has_timer && m->rtc.latch == 0 && value == 1) {
			rtc_update(&m->rtc);
			for (unsigned i = 0; i < 5; i++) {
				m->rtc.latched[i] = m->rtc.regs[i];
			}
		}
		m->rtc.latch = value;
		break;
	}
}

static void mbc5_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 12) {
	case 0:
	case 1:
		m->ram_enabled = (value & 0x0F) == 0x0A;
		break;
	case 2:
		m->rom_bank = (m->rom_bank & 0x100) | value;
		break;
	case 3:
		m->rom_bank = (m->rom_bank & 0xFF) | ((unsigned)(value & 1) << 8);
		break;
	case 4:
	case 5:
		m->ram_bank = value & (m->rumble ? 0x07 : 0x0F);
		break;
	default:
		break;
	}
}

static void huc_write(Mapper *m, uint16_t addr, uint8_t value) {
	switch (addr >> 13) {
	case 0:
		m->io_mode = value & 0x0F;
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
		m->ram_enabled = (value & 0x0F) == 0x0A;
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
		bank = m->mode ? m->bank2 : 0;
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

uint8_t mapper_read_ram(const Mapper *m, const uint8_t *ram, size_t ram_size, uint16_t addr) {
	switch (m->type) {
	case MBC_2:
		return m->ram_enabled ? (uint8_t)(ram[addr & 0x1FF] | 0xF0) : 0xFF;
	case MBC_3:
		if (!m->ram_enabled) {
			return 0xFF;
		}
		if (m->ram_bank >= 8) {
			return (m->has_timer && m->ram_bank <= 0x0C) ? m->rtc.latched[m->ram_bank - 8] : 0xFF;
		}
		if (m->ram_bank > 3) {
			return 0xFF;
		}
		break;
	case MBC_HUC1:
		if (m->io_mode == HUC_IR) {
			return 0xC0;
		}
		break;
	case MBC_HUC3:
		if (m->io_mode >= 0x0B && m->io_mode <= HUC_IR) {
			return 0x01;
		}
		if (m->io_mode != 0x0A && m->io_mode != 0x00) {
			return 0xFF;
		}
		break;
	default:
		if (!m->ram_enabled) {
			return 0xFF;
		}
		break;
	}

	return ram ? ram[ram_offset(m, ram_size, addr)] : 0xFF;
}

bool mapper_write_ram(Mapper *m, uint8_t *ram, size_t ram_size, uint16_t addr, uint8_t value) {
	switch (m->type) {
	case MBC_2:
		if (!m->ram_enabled) {
			return false;
		}
		ram[addr & 0x1FF] = value & 0x0F;
		return true;
	case MBC_3:
		if (!m->ram_enabled) {
			return false;
		}
		if (m->ram_bank >= 8) {
			if (!m->has_timer || m->ram_bank > 0x0C) {
				return false;
			}
			rtc_write(&m->rtc, m->ram_bank - 8, value);
			return true;
		}
		if (m->ram_bank > 3) {
			return false;
		}
		break;
	case MBC_HUC1:
		if (m->io_mode == HUC_IR) {
			return false;
		}
		break;
	case MBC_HUC3:
		if (m->io_mode != 0x0A) {
			return false;
		}
		break;
	default:
		if (!m->ram_enabled) {
			return false;
		}
		break;
	}

	if (!ram) {
		return false;
	}
	ram[ram_offset(m, ram_size, addr)] = value;
	return true;
}

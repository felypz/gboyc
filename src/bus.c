#include "bus.h"

#include <stdio.h>
#include <string.h>

#include "joypad.h"
#include "timer.h"

#define ADDR_JOYP 0xFF00
#define ADDR_SB	  0xFF01
#define ADDR_SC	  0xFF02
#define ADDR_IF	  0xFF0F
#define ADDR_DMA  0xFF46

static Cartridge *cartridge;
static Timer timer;
static Joypad joypad;
static uint8_t vram[0x2000];
static uint8_t wram[0x2000];
static uint8_t oam[0xA0];
static uint8_t io[0x80];
static uint8_t hram[0x7F];
static uint8_t ie;

static const struct {
	uint8_t reg;
	uint8_t value;
} IO_DEFAULTS[] = {
	{0x02, 0x7E}, {0x0F, 0xE1}, {0x10, 0x80}, {0x11, 0xBF}, {0x12, 0xF3}, {0x14, 0xBF},
	{0x16, 0x3F}, {0x19, 0xBF}, {0x1A, 0x7F}, {0x1B, 0xFF}, {0x1C, 0x9F}, {0x1E, 0xBF},
	{0x20, 0xFF}, {0x23, 0xBF}, {0x24, 0x77}, {0x25, 0xF3}, {0x26, 0xF1}, {0x40, 0x91},
	{0x41, 0x85}, {0x46, 0xFF}, {0x47, 0xFC}, {0x48, 0xFF}, {0x49, 0xFF},
};

void bus_init(Cartridge *cart) {
	cartridge = cart;
	memset(vram, 0, sizeof vram);
	memset(wram, 0, sizeof wram);
	memset(oam, 0, sizeof oam);
	memset(io, 0, sizeof io);
	memset(hram, 0, sizeof hram);
	ie = 0;
	timer_reset(&timer);
	joypad_reset(&joypad);

	for (size_t i = 0; i < sizeof IO_DEFAULTS / sizeof *IO_DEFAULTS; i++) {
		io[IO_DEFAULTS[i].reg] = IO_DEFAULTS[i].value;
	}
}

void bus_request_irq(unsigned irq) {
	io[ADDR_IF - 0xFF00] |= (uint8_t)(1u << irq);
}

void bus_tick(int cycles) {
	if (timer_step(&timer, cycles)) {
		bus_request_irq(IRQ_TIMER);
	}
}

void bus_set_joypad(uint8_t mask) {
	if (joypad_set(&joypad, mask)) {
		bus_request_irq(IRQ_JOYPAD);
	}
}

static uint8_t io_read(uint16_t addr) {
	if (addr == ADDR_JOYP) {
		return joypad_read(&joypad);
	}
	if (addr >= 0xFF04 && addr <= 0xFF07) {
		return timer_read(&timer, addr);
	}
	if (addr == ADDR_IF) {
		return io[addr - 0xFF00] | 0xE0;
	}
	return io[addr - 0xFF00];
}

static void io_write(uint16_t addr, uint8_t value) {
	if (addr == ADDR_JOYP) {
		if (joypad_write(&joypad, value)) {
			bus_request_irq(IRQ_JOYPAD);
		}
		return;
	}

	if (addr >= 0xFF04 && addr <= 0xFF07) {
		if (timer_write(&timer, addr, value)) {
			bus_request_irq(IRQ_TIMER);
		}
		return;
	}

	if (addr == ADDR_SC && (value & 0x81) == 0x81) {
		putchar(io[ADDR_SB - 0xFF00]);
		fflush(stdout);
		value &= 0x7F;
		bus_request_irq(IRQ_SERIAL);
	} else if (addr == ADDR_DMA) {
		for (uint16_t i = 0; i < sizeof oam; i++) {
			oam[i] = bus_read((uint16_t)((value << 8) + i));
		}
	}
	io[addr - 0xFF00] = value;
}

uint8_t bus_read(uint16_t addr) {
	if (addr < 0x8000) {
		return cartridge_read_rom(cartridge, addr);
	}
	if (addr < 0xA000) {
		return vram[addr & 0x1FFF];
	}
	if (addr < 0xC000) {
		return cartridge_read_ram(cartridge, addr);
	}
	if (addr < 0xFE00) {
		return wram[addr & 0x1FFF];
	}
	if (addr < 0xFEA0) {
		return oam[addr - 0xFE00];
	}
	if (addr < 0xFF00) {
		return 0xFF;
	}
	if (addr < 0xFF80) {
		return io_read(addr);
	}
	if (addr < 0xFFFF) {
		return hram[addr - 0xFF80];
	}
	return ie;
}

void bus_write(uint16_t addr, uint8_t value) {
	if (addr < 0x8000) {
		cartridge_write_rom(cartridge, addr, value);
	} else if (addr < 0xA000) {
		vram[addr & 0x1FFF] = value;
	} else if (addr < 0xC000) {
		cartridge_write_ram(cartridge, addr, value);
	} else if (addr < 0xFE00) {
		wram[addr & 0x1FFF] = value;
	} else if (addr < 0xFEA0) {
		oam[addr - 0xFE00] = value;
	} else if (addr >= 0xFFFF) {
		ie = value;
	} else if (addr >= 0xFF80) {
		hram[addr - 0xFF80] = value;
	} else if (addr >= 0xFF00) {
		io_write(addr, value);
	}
}

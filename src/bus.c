#include "bus.h"

#include <stdio.h>
#include <string.h>

#include "apu.h"
#include "joypad.h"
#include "ppu.h"
#include "timer.h"

#define ADDR_JOYP 0xFF00
#define ADDR_SB	  0xFF01
#define ADDR_SC	  0xFF02
#define ADDR_IF	  0xFF0F
#define ADDR_DMA  0xFF46

static Cartridge *cartridge;
static Timer timer;
static Joypad joypad;
static Ppu ppu;
static Apu apu;
static uint8_t wram[0x2000];
static uint8_t io[0x80];
static uint8_t hram[0x7F];
static uint8_t ie;

static const struct {
	uint8_t reg;
	uint8_t value;
} IO_DEFAULTS[] = {
	{0x02, 0x7E},
	{0x0F, 0xE1},
	{0x46, 0xFF},
};

static bool is_ppu_reg(uint16_t addr) {
	return addr >= 0xFF40 && addr <= 0xFF4B && addr != ADDR_DMA;
}

static bool is_apu_reg(uint16_t addr) {
	return addr >= 0xFF10 && addr <= 0xFF3F;
}

static void request_ppu_irqs(unsigned mask) {
	if (mask & PPU_IRQ_VBLANK) {
		bus_request_irq(IRQ_VBLANK);
	}
	if (mask & PPU_IRQ_STAT) {
		bus_request_irq(IRQ_STAT);
	}
}

void bus_init(Cartridge *cart) {
	cartridge = cart;
	memset(wram, 0, sizeof wram);
	memset(io, 0, sizeof io);
	memset(hram, 0, sizeof hram);
	ie = 0;
	timer_reset(&timer);
	joypad_reset(&joypad);
	ppu_reset(&ppu);
	apu_reset(&apu);

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
	request_ppu_irqs(ppu_step(&ppu, cycles));
	apu_step(&apu, cycles);
}

void bus_set_joypad(uint8_t mask) {
	if (joypad_set(&joypad, mask)) {
		bus_request_irq(IRQ_JOYPAD);
	}
}

bool bus_frame_ready(void) {
	return ppu_take_frame(&ppu);
}

const uint32_t *bus_framebuffer(void) {
	return ppu.framebuffer;
}

size_t bus_take_audio(const float **samples) {
	return apu_take_samples(&apu, samples);
}

static uint8_t io_read(uint16_t addr) {
	if (addr == ADDR_JOYP) {
		return joypad_read(&joypad);
	}
	if (addr >= 0xFF04 && addr <= 0xFF07) {
		return timer_read(&timer, addr);
	}
	if (is_apu_reg(addr)) {
		return apu_read(&apu, addr);
	}
	if (is_ppu_reg(addr)) {
		return ppu_read(&ppu, addr);
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

	if (is_apu_reg(addr)) {
		apu_write(&apu, addr, value);
		return;
	}

	if (is_ppu_reg(addr)) {
		request_ppu_irqs(ppu_write(&ppu, addr, value));
		return;
	}

	if (addr == ADDR_SC && (value & 0x81) == 0x81) {
		putchar(io[ADDR_SB - 0xFF00]);
		fflush(stdout);
		value &= 0x7F;
		bus_request_irq(IRQ_SERIAL);
	} else if (addr == ADDR_DMA) {
		for (uint16_t i = 0; i < sizeof ppu.oam; i++) {
			ppu.oam[i] = bus_read((uint16_t)((value << 8) + i));
		}
	}
	io[addr - 0xFF00] = value;
}

uint8_t bus_read(uint16_t addr) {
	if (addr < 0x8000) {
		return cartridge_read_rom(cartridge, addr);
	}
	if (addr < 0xA000) {
		return ppu_read_vram(&ppu, addr);
	}
	if (addr < 0xC000) {
		return cartridge_read_ram(cartridge, addr);
	}
	if (addr < 0xFE00) {
		return wram[addr & 0x1FFF];
	}
	if (addr < 0xFEA0) {
		return ppu_read_oam(&ppu, addr - 0xFE00);
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
		ppu_write_vram(&ppu, addr, value);
	} else if (addr < 0xC000) {
		cartridge_write_ram(cartridge, addr, value);
	} else if (addr < 0xFE00) {
		wram[addr & 0x1FFF] = value;
	} else if (addr < 0xFEA0) {
		ppu_write_oam(&ppu, addr - 0xFE00, value);
	} else if (addr >= 0xFFFF) {
		ie = value;
	} else if (addr >= 0xFF80) {
		hram[addr - 0xFF80] = value;
	} else if (addr >= 0xFF00) {
		io_write(addr, value);
	}
}

#include "bus.h"

#include <stdio.h>

#define ADDR_JOYP 0xFF00
#define ADDR_SB	  0xFF01
#define ADDR_SC	  0xFF02
#define ADDR_IF	  0xFF0F
#define ADDR_DMA  0xFF46

static bool is_ppu_reg(uint16_t addr) {
	return addr >= 0xFF40 && addr <= 0xFF4B && addr != ADDR_DMA;
}

static bool is_apu_reg(uint16_t addr) {
	return addr >= 0xFF10 && addr <= 0xFF3F;
}

static uint8_t io_read(const Gb *gb, uint16_t addr) {
	if (addr == ADDR_JOYP) {
		return joypad_read(&gb->joypad);
	}
	if (addr >= 0xFF04 && addr <= 0xFF07) {
		return timer_read(&gb->timer, addr);
	}
	if (is_apu_reg(addr)) {
		return apu_read(&gb->apu, addr);
	}
	if (is_ppu_reg(addr)) {
		return ppu_read(&gb->ppu, addr);
	}
	if (addr == ADDR_IF) {
		return gb->io[addr - 0xFF00] | 0xE0;
	}
	return gb->io[addr - 0xFF00];
}

static void io_write(Gb *gb, uint16_t addr, uint8_t value) {
	if (addr == ADDR_JOYP) {
		if (joypad_write(&gb->joypad, value)) {
			gb_request_irq(gb, IRQ_JOYPAD);
		}
		return;
	}

	if (addr >= 0xFF04 && addr <= 0xFF07) {
		if (timer_write(&gb->timer, addr, value)) {
			gb_request_irq(gb, IRQ_TIMER);
		}
		return;
	}

	if (is_apu_reg(addr)) {
		apu_write(&gb->apu, addr, value);
		return;
	}

	if (is_ppu_reg(addr)) {
		gb_request_ppu_irqs(gb, ppu_write(&gb->ppu, addr, value));
		return;
	}

	if (addr == ADDR_SC && (value & 0x81) == 0x81) {
		putchar(gb->io[ADDR_SB - 0xFF00]);
		fflush(stdout);
		value &= 0x7F;
		gb_request_irq(gb, IRQ_SERIAL);
	} else if (addr == ADDR_DMA) {
		for (uint16_t i = 0; i < sizeof gb->ppu.oam; i++) {
			gb->ppu.oam[i] = bus_read(gb, (uint16_t)((value << 8) + i));
		}
	}
	gb->io[addr - 0xFF00] = value;
}

uint8_t bus_read(const Gb *gb, uint16_t addr) {
	if (addr < 0x8000) {
		return cartridge_read_rom(gb->cart, addr);
	}
	if (addr < 0xA000) {
		return ppu_read_vram(&gb->ppu, addr);
	}
	if (addr < 0xC000) {
		return cartridge_read_ram(gb->cart, addr);
	}
	if (addr < 0xFE00) {
		return gb->wram[addr & 0x1FFF];
	}
	if (addr < 0xFEA0) {
		return ppu_read_oam(&gb->ppu, addr - 0xFE00);
	}
	if (addr < 0xFF00) {
		return 0xFF;
	}
	if (addr < 0xFF80) {
		return io_read(gb, addr);
	}
	if (addr < 0xFFFF) {
		return gb->hram[addr - 0xFF80];
	}
	return gb->ie;
}

void bus_write(Gb *gb, uint16_t addr, uint8_t value) {
	if (addr < 0x8000) {
		cartridge_write_rom(gb->cart, addr, value);
	} else if (addr < 0xA000) {
		ppu_write_vram(&gb->ppu, addr, value);
	} else if (addr < 0xC000) {
		cartridge_write_ram(gb->cart, addr, value);
	} else if (addr < 0xFE00) {
		gb->wram[addr & 0x1FFF] = value;
	} else if (addr < 0xFEA0) {
		ppu_write_oam(&gb->ppu, addr - 0xFE00, value);
	} else if (addr >= 0xFFFF) {
		gb->ie = value;
	} else if (addr >= 0xFF80) {
		gb->hram[addr - 0xFF80] = value;
	} else if (addr >= 0xFF00) {
		io_write(gb, addr, value);
	}
}

#include "bus.h"

#define ADDR_JOYP 0xFF00
#define ADDR_SB	  0xFF01
#define ADDR_SC	  0xFF02
#define ADDR_IF	  0xFF0F
#define ADDR_DMA  0xFF46
#define ADDR_BOOT 0xFF50

static bool is_ppu_reg(uint16_t addr) {
	return addr >= 0xFF40 && addr <= 0xFF4B && addr != ADDR_DMA;
}

static bool is_apu_reg(uint16_t addr) {
	return addr >= 0xFF10 && addr <= 0xFF3F;
}

static bool is_serial_reg(uint16_t addr) {
	return addr == ADDR_SB || addr == ADDR_SC;
}

static bool dma_blocks(const Gb *gb, uint16_t addr) {
	return gb->dma.active && gb->dma.delay == 0 && addr < 0xFF00;
}

static uint8_t io_read(const Gb *gb, uint16_t addr) {
	if (addr == ADDR_BOOT) {
		return 0xFF;
	}
	if (addr == ADDR_JOYP) {
		return joypad_read(&gb->joypad);
	}
	if (is_serial_reg(addr)) {
		return serial_read(&gb->serial, addr);
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
	if (is_serial_reg(addr)) {
		serial_write(&gb->serial, addr, value);
		return;
	}
	if (addr >= 0xFF04 && addr <= 0xFF07) {
		timer_write(&gb->timer, addr, value);
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
	if (addr == ADDR_BOOT) {
		if (value) {
			gb->boot_on = false;
		}
		return;
	}
	if (addr == ADDR_DMA) {
		gb_dma_start(gb, value);
	}
	gb->io[addr - 0xFF00] = value;
}

uint8_t bus_read(const Gb *gb, uint16_t addr) {
	if (dma_blocks(gb, addr)) {
		return 0xFF;
	}
	if (gb->boot_on && addr < BOOT_ROM_SIZE) {
		return gb->boot_rom[addr];
	}
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
	if (dma_blocks(gb, addr)) {
		return;
	}
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

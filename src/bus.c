#include "bus.h"

static bool is_ppu_reg(uint16_t addr) {
	return addr >= REG_LCDC && addr <= REG_WX && addr != REG_DMA;
}

static bool is_apu_reg(uint16_t addr) {
	return addr >= REG_NR10 && addr <= REG_WAVE_END;
}

static bool is_serial_reg(uint16_t addr) {
	return addr == REG_SB || addr == REG_SC;
}

static bool is_timer_reg(uint16_t addr) {
	return addr >= REG_DIV && addr <= REG_TAC;
}

/* During OAM DMA the CPU can only reach HRAM and the I/O page. */
static bool dma_blocks(const Gb *gb, uint16_t addr) {
	return gb->dma.active && gb->dma.delay == 0 && addr < MEM_IO_START;
}

/* WRAM and its echo are both 8 KiB-aligned mirrors, so masking maps either to WRAM. */
static uint16_t wram_index(uint16_t addr) {
	return addr & (MEM_WRAM_SIZE - 1);
}

/* Registers without a dedicated module are plain storage in gb->io. */
static uint8_t io_read(const Gb *gb, uint16_t addr) {
	if (addr == REG_BOOT) {
		return 0xFF;
	}
	if (addr == REG_JOYP) {
		return joypad_read(&gb->joypad);
	}
	if (is_serial_reg(addr)) {
		return serial_read(&gb->serial, addr);
	}
	if (is_timer_reg(addr)) {
		return timer_read(&gb->timer, addr);
	}
	if (is_apu_reg(addr)) {
		return apu_read(&gb->apu, addr);
	}
	if (is_ppu_reg(addr)) {
		return ppu_read(&gb->ppu, addr);
	}
	if (addr == REG_IF) {
		return gb->io[addr - MEM_IO_START] | ~IRQ_MASK;
	}
	return gb->io[addr - MEM_IO_START];
}

static void io_write(Gb *gb, uint16_t addr, uint8_t value) {
	if (addr == REG_JOYP) {
		if (joypad_write(&gb->joypad, value)) {
			gb_request_irq(gb, IRQ_JOYPAD);
		}
		return;
	}
	if (is_serial_reg(addr)) {
		serial_write(&gb->serial, addr, value);
		return;
	}
	if (is_timer_reg(addr)) {
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
	if (addr == REG_BOOT) {
		if (value) {
			gb->boot_rom_mapped = false;
		}
		return;
	}
	if (addr == REG_DMA) {
		gb_dma_start(gb, value);
	}
	gb->io[addr - MEM_IO_START] = value;
}

uint8_t bus_read(const Gb *gb, uint16_t addr) {
	if (dma_blocks(gb, addr)) {
		return 0xFF;
	}
	if (gb->boot_rom_mapped && addr < BOOT_ROM_SIZE) {
		return gb->boot_rom[addr];
	}
	if (addr <= MEM_ROM_END) {
		return cartridge_read_rom(gb->cart, addr);
	}
	if (addr <= MEM_VRAM_END) {
		return ppu_read_vram(&gb->ppu, addr - MEM_VRAM_START);
	}
	if (addr <= MEM_EXT_RAM_END) {
		return cartridge_read_ram(gb->cart, addr);
	}
	if (addr <= MEM_ECHO_END) {
		return gb->wram[wram_index(addr)];
	}
	if (addr <= MEM_OAM_END) {
		return ppu_read_oam(&gb->ppu, addr - MEM_OAM_START);
	}
	if (addr <= MEM_UNUSABLE_END) {
		return 0xFF;
	}
	if (addr <= MEM_IO_END) {
		return io_read(gb, addr);
	}
	if (addr <= MEM_HRAM_END) {
		return gb->hram[addr - MEM_HRAM_START];
	}
	return gb->ie;
}

void bus_write(Gb *gb, uint16_t addr, uint8_t value) {
	if (dma_blocks(gb, addr)) {
		return;
	}
	if (addr <= MEM_ROM_END) {
		cartridge_write_rom(gb->cart, addr, value);
	} else if (addr <= MEM_VRAM_END) {
		ppu_write_vram(&gb->ppu, addr - MEM_VRAM_START, value);
	} else if (addr <= MEM_EXT_RAM_END) {
		cartridge_write_ram(gb->cart, addr, value);
	} else if (addr <= MEM_ECHO_END) {
		gb->wram[wram_index(addr)] = value;
	} else if (addr <= MEM_OAM_END) {
		ppu_write_oam(&gb->ppu, addr - MEM_OAM_START, value);
	} else if (addr <= MEM_UNUSABLE_END) {
		/* writes are ignored */
	} else if (addr <= MEM_IO_END) {
		io_write(gb, addr, value);
	} else if (addr <= MEM_HRAM_END) {
		gb->hram[addr - MEM_HRAM_START] = value;
	} else {
		gb->ie = value;
	}
}

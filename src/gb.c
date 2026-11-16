#include "gb.h"

#include <string.h>

#define ADDR_IF	  0xFF0F
#define OAM_BYTES 160

static const struct {
	uint8_t reg;
	uint8_t value;
} IO_DEFAULTS[] = {
	{0x0F, 0xE1},
	{0x46, 0xFF},
};

void gb_init(Gb *gb, Cartridge *cart, const uint8_t *boot) {
	bool has_boot = boot != NULL;

	memset(gb, 0, sizeof *gb);
	gb->cart = cart;
	if (has_boot) {
		memcpy(gb->boot_rom, boot, BOOT_ROM_SIZE);
		gb->boot_on = true;
	}

	timer_reset(&gb->timer);
	joypad_reset(&gb->joypad);
	serial_reset(&gb->serial);
	ppu_reset(&gb->ppu);
	apu_reset(&gb->apu, has_boot);
	cpu_reset(&gb->cpu, gb, has_boot);

	if (has_boot) {
		gb->timer.div = 0;
		gb->ppu.lcdc = 0;
		gb->ppu.bgp = 0;
		return;
	}

	for (size_t i = 0; i < sizeof IO_DEFAULTS / sizeof *IO_DEFAULTS; i++) {
		gb->io[IO_DEFAULTS[i].reg] = IO_DEFAULTS[i].value;
	}
}

void gb_request_irq(Gb *gb, unsigned irq) {
	gb->io[ADDR_IF - 0xFF00] |= (uint8_t)(1u << irq);
}

void gb_request_ppu_irqs(Gb *gb, unsigned mask) {
	if (mask & PPU_IRQ_VBLANK) {
		gb_request_irq(gb, IRQ_VBLANK);
	}
	if (mask & PPU_IRQ_STAT) {
		gb_request_irq(gb, IRQ_STAT);
	}
}

void gb_dma_start(Gb *gb, uint8_t value) {
	gb->dma = (Dma){.active = true, .delay = 2, .pos = 0, .src = (uint16_t)(value << 8)};
}

static uint8_t dma_read(const Gb *gb, uint16_t addr) {
	if (addr >= 0xE000) {
		addr -= 0x2000;
	}
	if (addr < 0x8000) {
		return cartridge_read_rom(gb->cart, addr);
	}
	if (addr < 0xA000) {
		return gb->ppu.vram[addr & 0x1FFF];
	}
	if (addr < 0xC000) {
		return cartridge_read_ram(gb->cart, addr);
	}
	return gb->wram[addr & 0x1FFF];
}

static void dma_tick(Gb *gb) {
	Dma *d = &gb->dma;

	if (!d->active) {
		return;
	}
	if (d->delay) {
		d->delay--;
		return;
	}

	gb->ppu.oam[d->pos] = dma_read(gb, (uint16_t)(d->src + d->pos));
	if (++d->pos == OAM_BYTES) {
		d->active = false;
	}
}

void gb_tick(Gb *gb) {
	if (timer_step(&gb->timer)) {
		gb_request_irq(gb, IRQ_TIMER);
	}
	if (serial_step(&gb->serial)) {
		gb_request_irq(gb, IRQ_SERIAL);
	}
	gb_request_ppu_irqs(gb, ppu_step(&gb->ppu, 4));
	apu_step(&gb->apu, 4);
	dma_tick(gb);
}

void gb_run_frame(Gb *gb) {
	do {
		cpu_step(&gb->cpu);
	} while (!ppu_take_frame(&gb->ppu));
}

void gb_set_joypad(Gb *gb, uint8_t mask) {
	if (joypad_set(&gb->joypad, mask)) {
		gb_request_irq(gb, IRQ_JOYPAD);
	}
}

const uint32_t *gb_framebuffer(const Gb *gb) {
	return gb->ppu.framebuffer;
}

size_t gb_take_audio(Gb *gb, const float **samples) {
	return apu_take_samples(&gb->apu, samples);
}

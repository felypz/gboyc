#include "gb.h"

#include <string.h>

#define ADDR_IF 0xFF0F

static const struct {
	uint8_t reg;
	uint8_t value;
} IO_DEFAULTS[] = {
	{0x02, 0x7E},
	{0x0F, 0xE1},
	{0x46, 0xFF},
};

void gb_init(Gb *gb, Cartridge *cart) {
	memset(gb, 0, sizeof *gb);
	gb->cart = cart;

	timer_reset(&gb->timer);
	joypad_reset(&gb->joypad);
	ppu_reset(&gb->ppu);
	apu_reset(&gb->apu);
	cpu_reset(&gb->cpu, gb);

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

static void tick(Gb *gb, int cycles) {
	if (timer_step(&gb->timer, cycles)) {
		gb_request_irq(gb, IRQ_TIMER);
	}
	gb_request_ppu_irqs(gb, ppu_step(&gb->ppu, cycles));
	apu_step(&gb->apu, cycles);
}

void gb_run_frame(Gb *gb) {
	do {
		tick(gb, cpu_step(&gb->cpu));
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

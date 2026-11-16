#ifndef GBOYC_GB_H
#define GBOYC_GB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apu.h"
#include "cartridge.h"
#include "cpu.h"
#include "joypad.h"
#include "ppu.h"
#include "timer.h"

#define IRQ_VBLANK 0
#define IRQ_STAT   1
#define IRQ_TIMER  2
#define IRQ_SERIAL 3
#define IRQ_JOYPAD 4

typedef struct Gb {
	Cpu cpu;
	Timer timer;
	Joypad joypad;
	Ppu ppu;
	Apu apu;
	Cartridge *cart;
	uint8_t wram[0x2000];
	uint8_t hram[0x7F];
	uint8_t io[0x80];
	uint8_t ie;
} Gb;

void gb_init(Gb *gb, Cartridge *cart);
void gb_run_frame(Gb *gb);
void gb_set_joypad(Gb *gb, uint8_t mask);

void gb_request_irq(Gb *gb, unsigned irq);
void gb_request_ppu_irqs(Gb *gb, unsigned mask);

const uint32_t *gb_framebuffer(const Gb *gb);
size_t gb_take_audio(Gb *gb, const float **samples);

#endif

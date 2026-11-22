#ifndef GBOYC_GB_H
#define GBOYC_GB_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apu.h"
#include "boot.h"
#include "cartridge.h"
#include "cpu.h"
#include "joypad.h"
#include "memmap.h"
#include "ppu.h"
#include "serial.h"
#include "timer.h"
#include "timing.h"

/* Interrupt numbers (bit positions in IF / IE). */
#define IRQ_VBLANK 0
#define IRQ_STAT   1
#define IRQ_TIMER  2
#define IRQ_SERIAL 3
#define IRQ_JOYPAD 4

typedef struct {
	bool active;
	uint8_t delay;
	uint8_t pos;
	uint16_t src;
} Dma;

/* Bounded execution used by the stepper: longjmp(*escape) once max_mcycles is reached. */
typedef struct {
	jmp_buf *escape;
	unsigned max_mcycles;
	unsigned mcycles_done;
} GbStep;

typedef struct Gb {
	Cpu cpu;
	Timer timer;
	Joypad joypad;
	Serial serial;
	Ppu ppu;
	Apu apu;
	Dma dma;
	Cartridge *cart;
	uint8_t boot_rom[BOOT_ROM_SIZE];
	bool boot_rom_mapped; /* true until the boot ROM writes REG_BOOT */
	uint8_t wram[MEM_WRAM_SIZE];
	uint8_t hram[MEM_HRAM_SIZE];
	uint8_t io[MEM_IO_SIZE];
	uint8_t ie;
	GbStep step;
} Gb;

/* boot_rom == NULL starts directly in the post-boot state. */
void gb_init(Gb *gb, Cartridge *cart, const uint8_t *boot_rom);
/* One CPU M-cycle is bracketed by begin (before the bus access) and end (advances peripherals). */
void gb_mcycle_begin(Gb *gb);
void gb_mcycle_end(Gb *gb);
/* Runs until a frame is ready. Returns false if the frame budget was exhausted (guest stalled). */
bool gb_run_frame(Gb *gb);
bool gb_state_sane(const Gb *gb);
void gb_set_joypad(Gb *gb, uint8_t mask);

void gb_request_irq(Gb *gb, unsigned irq);
void gb_request_ppu_irqs(Gb *gb, unsigned mask);
void gb_dma_start(Gb *gb, uint8_t value);

const uint32_t *gb_framebuffer(const Gb *gb);
size_t gb_take_audio(Gb *gb, const float **samples);

#endif

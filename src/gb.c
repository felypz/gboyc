#include "gb.h"

#include <string.h>

/* Cap on cpu_step() calls per frame. A frame is 17556 M-cycles and every step takes at least one.
 */
#define MAX_STEPS_PER_FRAME 60000

/* Offset between the echo region and the WRAM it mirrors. */
#define ECHO_OFFSET (MEM_ECHO_START - MEM_WRAM_START)

/* I/O register values after the boot ROM, for registers stored in gb->io. */
static const struct {
	uint16_t reg;
	uint8_t value;
} POST_BOOT_IO[] = {
	{REG_IF, 0xE1},
	{REG_DMA, 0xFF},
};

void gb_init(Gb *gb, Cartridge *cart, const uint8_t *boot_rom) {
	bool has_boot_rom = boot_rom != NULL;

	memset(gb, 0, sizeof *gb);
	gb->cart = cart;
	if (has_boot_rom) {
		memcpy(gb->boot_rom, boot_rom, BOOT_ROM_SIZE);
		gb->boot_rom_mapped = true;
	}

	timer_reset(&gb->timer, has_boot_rom);
	joypad_reset(&gb->joypad);
	serial_reset(&gb->serial);
	ppu_reset(&gb->ppu, has_boot_rom);
	apu_reset(&gb->apu, has_boot_rom);
	cpu_reset(&gb->cpu, gb, has_boot_rom);

	if (has_boot_rom) {
		return;
	}
	for (size_t i = 0; i < sizeof POST_BOOT_IO / sizeof *POST_BOOT_IO; i++) {
		gb->io[POST_BOOT_IO[i].reg - MEM_IO_START] = POST_BOOT_IO[i].value;
	}
}

void gb_request_irq(Gb *gb, unsigned irq) {
	gb->io[REG_IF - MEM_IO_START] |= (uint8_t)(1u << irq);
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

/* DMA reads the source directly, bypassing the bus (and its DMA lockout). */
static uint8_t dma_read(const Gb *gb, uint16_t addr) {
	if (addr >= MEM_ECHO_START) {
		addr -= ECHO_OFFSET;
	}
	if (addr <= MEM_ROM_END) {
		return cartridge_read_rom(gb->cart, addr);
	}
	if (addr <= MEM_VRAM_END) {
		return gb->ppu.vram[addr - MEM_VRAM_START];
	}
	if (addr <= MEM_EXT_RAM_END) {
		return cartridge_read_ram(gb->cart, addr);
	}
	return gb->wram[addr - MEM_WRAM_START];
}

static void dma_mcycle(Gb *gb) {
	Dma *d = &gb->dma;

	if (!d->active) {
		return;
	}
	if (d->delay) {
		d->delay--;
		return;
	}

	gb->ppu.oam[d->pos] = dma_read(gb, (uint16_t)(d->src + d->pos));
	if (++d->pos == MEM_OAM_SIZE) {
		d->active = false;
	}
}

/* Called by the CPU at the start of every M-cycle, before its bus access. */
void gb_mcycle_begin(Gb *gb) {
	if (gb->step.escape && gb->step.mcycles_done++ == gb->step.max_mcycles) {
		longjmp(*gb->step.escape, 1);
	}
}

/* Advances every peripheral by one M-cycle, after the CPU's bus access. */
void gb_mcycle_end(Gb *gb) {
	if (timer_step(&gb->timer)) {
		gb_request_irq(gb, IRQ_TIMER);
	}
	if (serial_step(&gb->serial)) {
		gb_request_irq(gb, IRQ_SERIAL);
	}
	gb_request_ppu_irqs(gb, ppu_mcycle(&gb->ppu));
	apu_step(&gb->apu, GB_DOTS_PER_MCYCLE);
	dma_mcycle(gb);
}

bool gb_run_frame(Gb *gb) {
	for (unsigned i = 0; i < MAX_STEPS_PER_FRAME; i++) {
		cpu_step(&gb->cpu);
		if (ppu_take_frame(&gb->ppu)) {
			return true;
		}
	}
	return false;
}

bool gb_state_sane(const Gb *gb) {
	const Dma *d = &gb->dma;

	return ppu_state_sane(&gb->ppu) && apu_state_sane(&gb->apu) && timer_state_sane(&gb->timer) &&
		   (!d->active || d->pos < MEM_OAM_SIZE) && d->delay <= 2 && gb->serial.bits <= 8 &&
		   gb->cpu.trace_pos < CPU_TRACE;
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

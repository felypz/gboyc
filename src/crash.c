#include "crash.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "cartridge.h"
#include "log.h"

#define DUMP_ROWS	3
#define STACK_WORDS 8

/* add_line() silently drops lines past CRASH_LINES, so check the worst case at build time. */
#define REGISTER_LINES		  3 /* add_registers() */
#define EMULATOR_REPORT_LINES 3 /* detail + two fixed notes, see crash_emulator() */
_Static_assert(CRASH_LINES >= REGISTER_LINES + EMULATOR_REPORT_LINES,
			   "CRASH_LINES too small for the longest crash report");

static void add_line(CrashReport *r, const char *fmt, ...) {
	if (r->line_count >= CRASH_LINES) {
		return;
	}

	va_list args;

	va_start(args, fmt);
	vsnprintf(r->lines[r->line_count++], CRASH_LINE_LEN, fmt, args);
	va_end(args);
}

static void add_registers(CrashReport *r, const Gb *gb) {
	const Cpu *c = &gb->cpu;
	unsigned irq_flags = gb->io[REG_IF - MEM_IO_START] | 0xE0; /* unused IF bits read as 1 */

	add_line(r, "PC %04X  SP %04X  AF %04X  BC %04X", (unsigned)c->pc, (unsigned)c->sp,
			 (unsigned)c->af, (unsigned)c->bc);
	add_line(r, "DE %04X  HL %04X  IME %d  IE %02X  IF %02X", (unsigned)c->de, (unsigned)c->hl,
			 c->ime ? 1 : 0, (unsigned)gb->ie, irq_flags);
	add_line(r, "ROM bank %u  LY %u  LCDC %02X  STAT %02X", cartridge_rom_bank(gb->cart),
			 (unsigned)gb->ppu.ly, (unsigned)gb->ppu.lcdc, (unsigned)ppu_read(&gb->ppu, REG_STAT));
}

bool crash_detect_game(const Gb *gb, CrashReport *r) {
	const Cpu *c = &gb->cpu;
	/* HALT with no interrupt enabled can never wake up. */
	bool lockup = !c->hung && c->halted && (gb->ie & IRQ_MASK) == 0;

	if (!c->hung && !lockup) {
		return false;
	}

	memset(r, 0, sizeof *r);
	r->kind = CRASH_GAME;
	snprintf(r->title, sizeof r->title, "Game crashed");

	if (c->hung) {
		snprintf(r->reason, sizeof r->reason, "Illegal opcode $%02X at $%04X",
				 (unsigned)c->fault_op, (unsigned)c->fault_pc);
	} else {
		snprintf(r->reason, sizeof r->reason, "Halted, interrupts disabled");
	}

	add_registers(r, gb);
	if (c->hung) {
		uint16_t pc = c->fault_pc;

		add_line(r, "Bytes at $%04X: %02X %02X %02X %02X", (unsigned)pc, (unsigned)bus_read(gb, pc),
				 (unsigned)bus_read(gb, (uint16_t)(pc + 1)),
				 (unsigned)bus_read(gb, (uint16_t)(pc + 2)),
				 (unsigned)bus_read(gb, (uint16_t)(pc + 3)));
	} else {
		add_line(r, "IE is 0, so nothing can wake the CPU up");
	}
	return true;
}

void crash_emulator(CrashReport *r, const Gb *gb, const char *reason, const char *detail) {
	memset(r, 0, sizeof *r);
	r->kind = CRASH_EMULATOR;
	snprintf(r->title, sizeof r->title, "Emulator error");
	snprintf(r->reason, sizeof r->reason, "%s", reason);

	add_line(r, "%s", detail);
	add_line(r, "The emulated machine may be corrupted.");
	add_line(r, "The battery save on disk was left untouched.");
	add_registers(r, gb);
}

static void log_dump_row(const Gb *gb, uint16_t addr) {
	char line[16 * 3 + 1];
	size_t len = 0;

	for (unsigned i = 0; i < 16; i++) {
		len += (size_t)snprintf(line + len, sizeof line - len, " %02X",
								(unsigned)bus_read(gb, (uint16_t)(addr + i)));
	}
	log_error("  $%04X:%s", (unsigned)addr, line);
}

static void print_dump(const Gb *gb) {
	const Cpu *c = &gb->cpu;
	uint16_t base = c->pc & 0xFFF0;

	log_error("--- dump ---");
	log_error("halted=%d stopped=%d halt_bug=%d hung=%d ei_delay=%u", c->halted ? 1 : 0,
			  c->stopped ? 1 : 0, c->halt_bug ? 1 : 0, c->hung ? 1 : 0, (unsigned)c->ei_delay);
	log_error("LCDC %02X STAT %02X SCY %02X SCX %02X LY %02X LYC %02X",
			  (unsigned)ppu_read(&gb->ppu, REG_LCDC), (unsigned)ppu_read(&gb->ppu, REG_STAT),
			  (unsigned)ppu_read(&gb->ppu, REG_SCY), (unsigned)ppu_read(&gb->ppu, REG_SCX),
			  (unsigned)ppu_read(&gb->ppu, REG_LY), (unsigned)ppu_read(&gb->ppu, REG_LYC));

	log_error("memory around PC ($%04X):", (unsigned)c->pc);
	for (int row = 0; row < DUMP_ROWS; row++) {
		log_dump_row(gb, (uint16_t)(base + (row - 1) * 16));
	}

	log_error("stack (SP=$%04X):", (unsigned)c->sp);
	for (unsigned i = 0; i < STACK_WORDS; i++) {
		uint16_t addr = (uint16_t)(c->sp + i * 2);
		unsigned word =
			(unsigned)bus_read(gb, addr) | ((unsigned)bus_read(gb, (uint16_t)(addr + 1)) << 8);

		log_error("  $%04X: %04X", (unsigned)addr, word);
	}

	log_error("last %d instructions (oldest first):", CPU_TRACE);
	for (unsigned i = 0; i < CPU_TRACE; i++) {
		unsigned idx = (c->trace_pos + i) & (CPU_TRACE - 1);

		log_error("  $%04X  opcode %02X", (unsigned)c->trace_pc[idx], (unsigned)c->trace_op[idx]);
	}
}

void crash_print(const CrashReport *r, const Gb *gb, bool full) {
	log_error("%s: %s", r->title, r->reason);
	for (int i = 0; i < r->line_count; i++) {
		log_error("  %s", r->lines[i]);
	}

	if (full) {
		print_dump(gb);
	} else {
		log_error("run with -v for a full dump");
	}
}

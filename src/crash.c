#include "crash.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "cartridge.h"

#define IRQ_ALL		0x1F
#define DUMP_ROWS	3
#define STACK_WORDS 8

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

	add_line(r, "PC %04X  SP %04X  AF %04X  BC %04X", (unsigned)c->pc, (unsigned)c->sp,
			 (unsigned)c->af, (unsigned)c->bc);
	add_line(r, "DE %04X  HL %04X  IME %d  IE %02X  IF %02X", (unsigned)c->de, (unsigned)c->hl,
			 c->ime ? 1 : 0, (unsigned)gb->ie, (unsigned)(gb->io[0x0F] | 0xE0));
	add_line(r, "ROM bank %u  LY %u  LCDC %02X  STAT %02X", cartridge_rom_bank(gb->cart),
			 (unsigned)gb->ppu.ly, (unsigned)gb->ppu.lcdc, (unsigned)ppu_read(&gb->ppu, 0xFF41));
}

bool crash_detect_game(const Gb *gb, CrashReport *r) {
	const Cpu *c = &gb->cpu;
	bool lockup = !c->hung && c->halted && (gb->ie & IRQ_ALL) == 0;

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

static void dump_row(const Gb *gb, uint16_t addr) {
	fprintf(stderr, "gboyc:   $%04X:", (unsigned)addr);
	for (unsigned i = 0; i < 16; i++) {
		fprintf(stderr, " %02X", (unsigned)bus_read(gb, (uint16_t)(addr + i)));
	}
	fputc('\n', stderr);
}

static void print_dump(const Gb *gb) {
	const Cpu *c = &gb->cpu;
	uint16_t base = c->pc & 0xFFF0;

	fprintf(stderr, "gboyc: --- dump ---\n");
	fprintf(stderr, "gboyc: halted=%d stopped=%d halt_bug=%d hung=%d ei_delay=%u\n",
			c->halted ? 1 : 0, c->stopped ? 1 : 0, c->halt_bug ? 1 : 0, c->hung ? 1 : 0,
			(unsigned)c->ei_delay);
	fprintf(stderr, "gboyc: LCDC %02X STAT %02X SCY %02X SCX %02X LY %02X LYC %02X\n",
			(unsigned)ppu_read(&gb->ppu, 0xFF40), (unsigned)ppu_read(&gb->ppu, 0xFF41),
			(unsigned)ppu_read(&gb->ppu, 0xFF42), (unsigned)ppu_read(&gb->ppu, 0xFF43),
			(unsigned)ppu_read(&gb->ppu, 0xFF44), (unsigned)ppu_read(&gb->ppu, 0xFF45));

	fprintf(stderr, "gboyc: memory around PC ($%04X):\n", (unsigned)c->pc);
	for (int row = 0; row < DUMP_ROWS; row++) {
		dump_row(gb, (uint16_t)(base + (row - 1) * 16));
	}

	fprintf(stderr, "gboyc: stack (SP=$%04X):\n", (unsigned)c->sp);
	for (unsigned i = 0; i < STACK_WORDS; i++) {
		uint16_t addr = (uint16_t)(c->sp + i * 2);
		unsigned word =
			(unsigned)bus_read(gb, addr) | ((unsigned)bus_read(gb, (uint16_t)(addr + 1)) << 8);

		fprintf(stderr, "gboyc:   $%04X: %04X\n", (unsigned)addr, word);
	}

	fprintf(stderr, "gboyc: last %d instructions (oldest first):\n", CPU_TRACE);
	for (unsigned i = 0; i < CPU_TRACE; i++) {
		unsigned idx = (c->trace_pos + i) & (CPU_TRACE - 1);

		fprintf(stderr, "gboyc:   $%04X  opcode %02X\n", (unsigned)c->trace_pc[idx],
				(unsigned)c->trace_op[idx]);
	}
}

void crash_print(const CrashReport *r, const Gb *gb, bool full) {
	fprintf(stderr, "gboyc: %s: %s\n", r->title, r->reason);
	for (int i = 0; i < r->line_count; i++) {
		fprintf(stderr, "gboyc:   %s\n", r->lines[i]);
	}

	if (full) {
		print_dump(gb);
	} else {
		fprintf(stderr, "gboyc: run with -v for a full dump\n");
	}
}

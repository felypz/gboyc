#include "cpu.h"

#include <stddef.h>

#include "bus.h"

#define FLAG_Z	   0x80
#define FLAG_N	   0x40
#define FLAG_H	   0x20
#define FLAG_C	   0x10
#define FLAGS_MASK 0xF0 /* the low nibble of F is hard-wired to zero */

#define OPERAND_HL_MEM	6 /* register index 6 means "(HL)", not a register */
#define LDH_BASE		MEM_IO_START
#define IRQ_VECTOR_BASE 0x40
#define IRQ_VECTOR_STEP 8

/*
 * Opcodes are decoded as op = xx yyy zzz:
 *   x = op >> 6, y = (op >> 3) & 7, z = op & 7,
 *   p = y >> 1, q = y & 1 (register pair / direction selectors).
 */

typedef enum {
	ALU_ADD,
	ALU_ADC,
	ALU_SUB,
	ALU_SBC,
	ALU_AND,
	ALU_XOR,
	ALU_OR,
	ALU_CP,
} AluOp;

typedef enum {
	SHIFT_RLC,
	SHIFT_RRC,
	SHIFT_RL,
	SHIFT_RR,
	SHIFT_SLA,
	SHIFT_SRA,
	SHIFT_SWAP,
	SHIFT_SRL,
} ShiftOp;

typedef enum {
	ACC_NONE,
	ACC_READ,
	ACC_WRITE,
} Access;

static uint8_t flags(bool z, bool n, bool h, bool cy) {
	return (z ? FLAG_Z : 0) | (n ? FLAG_N : 0) | (h ? FLAG_H : 0) | (cy ? FLAG_C : 0);
}

static uint8_t pending_irqs(const Cpu *cpu) {
	return bus_read(cpu->gb, REG_IE) & bus_read(cpu->gb, REG_IF) & IRQ_MASK;
}

/* -- M-cycle primitives -- */

/* An M-cycle without a bus access. */
static void idle_mcycle(Cpu *cpu) {
	gb_mcycle_begin(cpu->gb);
	gb_mcycle_end(cpu->gb);
}

static bool in_oam(uint16_t addr) {
	return (addr >> 8) == (MEM_OAM_START >> 8);
}

/*
 * DMG OAM corruption bug: touching OAM (or putting an OAM address on the
 * address bus through the inc/dec unit) while the PPU scans it glitches OAM.
 */
static void oam_bug(Cpu *cpu, Access acc, uint16_t addr, bool idu, uint16_t idu_addr) {
	bool hit = acc != ACC_NONE && in_oam(addr);
	bool glitch = idu && in_oam(idu_addr);

	if (hit && acc == ACC_READ) {
		ppu_oam_bug(&cpu->gb->ppu, glitch ? OAM_BUG_READ_IDU : OAM_BUG_READ);
	} else if (hit || glitch) {
		ppu_oam_bug(&cpu->gb->ppu, OAM_BUG_WRITE);
	}
}

static uint8_t mcycle(Cpu *cpu, Access acc, uint16_t addr, uint8_t value, bool idu,
					  uint16_t idu_addr) {
	uint8_t result = 0;

	gb_mcycle_begin(cpu->gb);
	oam_bug(cpu, acc, addr, idu, idu_addr);
	if (acc == ACC_READ) {
		result = bus_read(cpu->gb, addr);
	} else if (acc == ACC_WRITE) {
		bus_write(cpu->gb, addr, value);
	}
	gb_mcycle_end(cpu->gb);
	return result;
}

static uint8_t rd(Cpu *cpu, uint16_t addr) {
	return mcycle(cpu, ACC_READ, addr, 0, false, 0);
}

static void wr(Cpu *cpu, uint16_t addr, uint8_t value) {
	mcycle(cpu, ACC_WRITE, addr, value, false, 0);
}

/* Same as rd/wr, but the inc/dec unit also drives the same address. */
static uint8_t rd_idu(Cpu *cpu, uint16_t addr) {
	return mcycle(cpu, ACC_READ, addr, 0, true, addr);
}

static void wr_idu(Cpu *cpu, uint16_t addr, uint8_t value) {
	mcycle(cpu, ACC_WRITE, addr, value, true, addr);
}

/* An M-cycle that only runs the inc/dec unit on `value` (e.g. INC rr, PUSH). */
static void idu_mcycle(Cpu *cpu, uint16_t value) {
	mcycle(cpu, ACC_NONE, 0, 0, true, value);
}

/* -- operand access -- */

static uint8_t fetch8(Cpu *cpu) {
	uint8_t value;

	if (cpu->halt_bug) {
		/* HALT bug: the byte after HALT is read twice because PC doesn't advance. */
		value = rd(cpu, cpu->pc);
		cpu->halt_bug = false;
	} else {
		value = rd_idu(cpu, cpu->pc);
		cpu->pc++;
	}
	return value;
}

static uint16_t fetch16(Cpu *cpu) {
	uint8_t lo = fetch8(cpu);
	uint8_t hi = fetch8(cpu);
	return (uint16_t)((hi << 8) | lo);
}

static void push16(Cpu *cpu, uint16_t value) {
	idu_mcycle(cpu, cpu->sp);
	cpu->sp--;
	wr_idu(cpu, cpu->sp, value >> 8);
	cpu->sp--;
	wr(cpu, cpu->sp, value & 0xFF);
}

static uint16_t pop16(Cpu *cpu) {
	uint8_t lo = rd_idu(cpu, cpu->sp);
	cpu->sp++;
	uint8_t hi = rd(cpu, cpu->sp);
	cpu->sp++;
	return (uint16_t)((hi << 8) | lo);
}

/* 8-bit register by opcode index: B C D E H L (HL) A. Index 6 has no register. */
static uint8_t *reg8(Cpu *cpu, unsigned i) {
	switch (i) {
	case 0:
		return &cpu->b;
	case 1:
		return &cpu->c;
	case 2:
		return &cpu->d;
	case 3:
		return &cpu->e;
	case 4:
		return &cpu->h;
	case 5:
		return &cpu->l;
	case 7:
		return &cpu->a;
	default:
		return NULL;
	}
}

static uint8_t get8(Cpu *cpu, unsigned i) {
	return i == OPERAND_HL_MEM ? rd(cpu, cpu->hl) : *reg8(cpu, i);
}

static void set8(Cpu *cpu, unsigned i, uint8_t value) {
	if (i == OPERAND_HL_MEM) {
		wr(cpu, cpu->hl, value);
	} else {
		*reg8(cpu, i) = value;
	}
}

/* 16-bit register pair table used by most opcodes: BC DE HL SP. */
static uint16_t *reg16(Cpu *cpu, unsigned p) {
	switch (p) {
	case 0:
		return &cpu->bc;
	case 1:
		return &cpu->de;
	case 2:
		return &cpu->hl;
	default:
		return &cpu->sp;
	}
}

/* 16-bit register pair table used by PUSH/POP: BC DE HL AF. */
static uint16_t *reg16_stack(Cpu *cpu, unsigned p) {
	return p == 3 ? &cpu->af : reg16(cpu, p);
}

static bool cond(const Cpu *cpu, unsigned cc) {
	bool set = cpu->f & (cc < 2 ? FLAG_Z : FLAG_C);
	return (cc & 1) ? set : !set;
}

/* -- ALU -- */

static uint8_t add8(Cpu *cpu, uint8_t v, unsigned carry) {
	unsigned r = cpu->a + v + carry;
	cpu->f = flags((r & 0xFF) == 0, false, (cpu->a ^ v ^ r) & 0x10, r > 0xFF);
	return (uint8_t)r;
}

static uint8_t sub8(Cpu *cpu, uint8_t v, unsigned carry) {
	unsigned r = cpu->a - v - carry;
	cpu->f = flags((r & 0xFF) == 0, true, (cpu->a ^ v ^ r) & 0x10, r > 0xFF);
	return (uint8_t)r;
}

static void alu(Cpu *cpu, AluOp op, uint8_t v) {
	unsigned carry = (cpu->f & FLAG_C) ? 1 : 0;

	switch (op) {
	case ALU_ADD:
		cpu->a = add8(cpu, v, 0);
		break;
	case ALU_ADC:
		cpu->a = add8(cpu, v, carry);
		break;
	case ALU_SUB:
		cpu->a = sub8(cpu, v, 0);
		break;
	case ALU_SBC:
		cpu->a = sub8(cpu, v, carry);
		break;
	case ALU_AND:
		cpu->a &= v;
		cpu->f = flags(cpu->a == 0, false, true, false);
		break;
	case ALU_XOR:
		cpu->a ^= v;
		cpu->f = flags(cpu->a == 0, false, false, false);
		break;
	case ALU_OR:
		cpu->a |= v;
		cpu->f = flags(cpu->a == 0, false, false, false);
		break;
	case ALU_CP:
		sub8(cpu, v, 0);
		break;
	}
}

static uint8_t inc8(Cpu *cpu, uint8_t v) {
	uint8_t r = v + 1;
	cpu->f = (cpu->f & FLAG_C) | flags(r == 0, false, (v & 0xF) == 0xF, false);
	return r;
}

static uint8_t dec8(Cpu *cpu, uint8_t v) {
	uint8_t r = v - 1;
	cpu->f = (cpu->f & FLAG_C) | flags(r == 0, true, (v & 0xF) == 0, false);
	return r;
}

static void add_hl(Cpu *cpu, uint16_t v) {
	unsigned r = cpu->hl + v;
	bool half = ((cpu->hl & 0xFFF) + (v & 0xFFF)) > 0xFFF;
	cpu->f = (cpu->f & FLAG_Z) | flags(false, false, half, r > 0xFFFF);
	cpu->hl = (uint16_t)r;
	idle_mcycle(cpu);
}

/* SP + signed immediate, shared by ADD SP,e8 and LD HL,SP+e8. Sets the flags. */
static uint16_t sp_plus_e8(Cpu *cpu) {
	uint8_t e = fetch8(cpu);
	bool half = ((cpu->sp & 0xF) + (e & 0xF)) > 0xF;
	bool carry = ((cpu->sp & 0xFF) + e) > 0xFF;
	cpu->f = flags(false, false, half, carry);
	return (uint16_t)(cpu->sp + (int8_t)e);
}

static uint8_t shift(Cpu *cpu, ShiftOp op, uint8_t v) {
	unsigned old = (cpu->f & FLAG_C) ? 1 : 0;
	uint8_t r;
	bool cy;

	switch (op) {
	case SHIFT_RLC:
		cy = v >> 7;
		r = (uint8_t)((v << 1) | cy);
		break;
	case SHIFT_RRC:
		cy = v & 1;
		r = (uint8_t)((v >> 1) | (cy << 7));
		break;
	case SHIFT_RL:
		cy = v >> 7;
		r = (uint8_t)((v << 1) | old);
		break;
	case SHIFT_RR:
		cy = v & 1;
		r = (uint8_t)((v >> 1) | (old << 7));
		break;
	case SHIFT_SLA:
		cy = v >> 7;
		r = (uint8_t)(v << 1);
		break;
	case SHIFT_SRA:
		cy = v & 1;
		r = (uint8_t)((v >> 1) | (v & 0x80));
		break;
	case SHIFT_SWAP:
		cy = false;
		r = (uint8_t)((v << 4) | (v >> 4));
		break;
	default: /* SHIFT_SRL */
		cy = v & 1;
		r = v >> 1;
		break;
	}

	cpu->f = flags(r == 0, false, false, cy);
	return r;
}

static void daa(Cpu *cpu) {
	uint8_t adj = 0;
	bool cy = cpu->f & FLAG_C;

	if (cpu->f & FLAG_N) {
		if (cpu->f & FLAG_H) {
			adj |= 0x06;
		}
		if (cy) {
			adj |= 0x60;
		}
		cpu->a -= adj;
	} else {
		if ((cpu->f & FLAG_H) || (cpu->a & 0x0F) > 9) {
			adj |= 0x06;
		}
		if (cy || cpu->a > 0x99) {
			adj |= 0x60;
			cy = true;
		}
		cpu->a += adj;
	}

	cpu->f = (cpu->f & FLAG_N) | flags(cpu->a == 0, false, false, cy);
}

/* -- control flow -- */

static void jr(Cpu *cpu, bool take) {
	int8_t e = (int8_t)fetch8(cpu);
	if (take) {
		idle_mcycle(cpu);
		cpu->pc = (uint16_t)(cpu->pc + e);
	}
}

static void jp(Cpu *cpu, bool take) {
	uint16_t addr = fetch16(cpu);
	if (take) {
		idle_mcycle(cpu);
		cpu->pc = addr;
	}
}

static void call(Cpu *cpu, bool take) {
	uint16_t addr = fetch16(cpu);
	if (take) {
		push16(cpu, cpu->pc);
		cpu->pc = addr;
	}
}

static void ret(Cpu *cpu, bool take) {
	if (take) {
		cpu->pc = pop16(cpu);
		idle_mcycle(cpu);
	}
}

static void halt(Cpu *cpu) {
	if (!cpu->ime && pending_irqs(cpu)) {
		cpu->halt_bug = true;
	} else {
		cpu->halted = true;
	}
}

/* -- instruction decoding -- */

/* 0xCB-prefixed opcodes: rotates/shifts (x=0), BIT (x=1), RES (x=2), SET (x=3). */
static void exec_cb(Cpu *cpu) {
	uint8_t op = fetch8(cpu);
	unsigned x = op >> 6;
	unsigned y = (op >> 3) & 7;
	unsigned z = op & 7;

	uint8_t value = get8(cpu, z);

	switch (x) {
	case 0:
		set8(cpu, z, shift(cpu, (ShiftOp)y, value));
		break;

	case 1:
		cpu->f = (cpu->f & FLAG_C) | FLAG_H | ((value & (1u << y)) ? 0 : FLAG_Z);
		break;

	case 2:
		set8(cpu, z, value & ~(1u << y));
		break;

	case 3:
		set8(cpu, z, value | (1u << y));
		break;
	}
}

/* x = 0: misc, 16-bit loads/arithmetic, 8-bit INC/DEC/LD imm, accumulator ops. */
static void exec_block0(Cpu *cpu, unsigned y, unsigned z) {
	unsigned p = y >> 1;
	bool q = y & 1;

	switch (z) {
	case 0:
		switch (y) {
		case 0: /* NOP */
			break;
		case 1: { /* LD (a16),SP */
			uint16_t addr = fetch16(cpu);
			wr(cpu, addr, cpu->sp & 0xFF);
			wr(cpu, addr + 1, cpu->sp >> 8);
			break;
		}
		case 2: /* STOP */
			fetch8(cpu);
			bus_write(cpu->gb, REG_DIV, 0);
			cpu->stopped = true;
			break;
		case 3:
			jr(cpu, true);
			break;
		default:
			jr(cpu, cond(cpu, y - 4));
			break;
		}
		break;

	case 1:
		if (q) {
			add_hl(cpu, *reg16(cpu, p));
		} else {
			*reg16(cpu, p) = fetch16(cpu);
		}
		break;

	case 2: { /* LD (BC)/(DE)/(HL+)/(HL-) <-> A */
		uint16_t addr = cpu->hl;
		bool idu = false;

		if (p == 0) {
			addr = cpu->bc;
		} else if (p == 1) {
			addr = cpu->de;
		} else if (p == 2) {
			cpu->hl++;
			idu = true;
		} else {
			cpu->hl--;
			idu = true;
		}

		if (q) {
			cpu->a = idu ? rd_idu(cpu, addr) : rd(cpu, addr);
		} else if (idu) {
			wr_idu(cpu, addr, cpu->a);
		} else {
			wr(cpu, addr, cpu->a);
		}
		break;
	}

	case 3: { /* INC rr / DEC rr */
		uint16_t *reg = reg16(cpu, p);

		idu_mcycle(cpu, *reg);
		*reg += q ? -1 : 1;
		break;
	}

	case 4:
		set8(cpu, y, inc8(cpu, get8(cpu, y)));
		break;

	case 5:
		set8(cpu, y, dec8(cpu, get8(cpu, y)));
		break;

	case 6:
		set8(cpu, y, fetch8(cpu));
		break;

	default: /* RLCA RRCA RLA RRA DAA CPL SCF CCF */
		switch (y) {
		case 0:
		case 1:
		case 2:
		case 3:
			cpu->a = shift(cpu, (ShiftOp)y, cpu->a);
			cpu->f &= FLAG_C; /* unlike the CB versions, Z is always cleared */
			break;
		case 4:
			daa(cpu);
			break;
		case 5:
			cpu->a = ~cpu->a;
			cpu->f |= FLAG_N | FLAG_H;
			break;
		case 6:
			cpu->f = (cpu->f & FLAG_Z) | FLAG_C;
			break;
		default:
			cpu->f = (cpu->f & FLAG_Z) | (~cpu->f & FLAG_C);
			break;
		}
		break;
	}
}

/* x = 3: stack, jumps, calls, high-page loads, ALU immediates, RST. */
static void exec_block3(Cpu *cpu, unsigned y, unsigned z) {
	unsigned p = y >> 1;
	bool q = y & 1;

	switch (z) {
	case 0:
		switch (y) {
		case 4: /* LDH (a8),A */
			wr(cpu, LDH_BASE | fetch8(cpu), cpu->a);
			break;
		case 5: /* ADD SP,e8 */
			cpu->sp = sp_plus_e8(cpu);
			idle_mcycle(cpu);
			idle_mcycle(cpu);
			break;
		case 6: /* LDH A,(a8) */
			cpu->a = rd(cpu, LDH_BASE | fetch8(cpu));
			break;
		case 7: /* LD HL,SP+e8 */
			cpu->hl = sp_plus_e8(cpu);
			idle_mcycle(cpu);
			break;
		default: /* RET cc */
			idle_mcycle(cpu);
			ret(cpu, cond(cpu, y));
			break;
		}
		break;

	case 1:
		if (!q) { /* POP rr */
			*reg16_stack(cpu, p) = pop16(cpu);
			if (p == 3) {
				cpu->f &= FLAGS_MASK;
			}
		} else if (p == 0) { /* RET */
			ret(cpu, true);
		} else if (p == 1) { /* RETI */
			ret(cpu, true);
			cpu->ime = true;
		} else if (p == 2) { /* JP HL */
			cpu->pc = cpu->hl;
		} else { /* LD SP,HL */
			cpu->sp = cpu->hl;
			idle_mcycle(cpu);
		}
		break;

	case 2:
		switch (y) {
		case 4: /* LDH (C),A */
			wr(cpu, LDH_BASE | cpu->c, cpu->a);
			break;
		case 5: /* LD (a16),A */
			wr(cpu, fetch16(cpu), cpu->a);
			break;
		case 6: /* LDH A,(C) */
			cpu->a = rd(cpu, LDH_BASE | cpu->c);
			break;
		case 7: /* LD A,(a16) */
			cpu->a = rd(cpu, fetch16(cpu));
			break;
		default: /* JP cc,a16 */
			jp(cpu, cond(cpu, y));
			break;
		}
		break;

	case 3:
		switch (y) {
		case 0:
			jp(cpu, true);
			break;
		case 1:
			exec_cb(cpu);
			break;
		case 6: /* DI */
			cpu->ime = false;
			cpu->ei_delay = 0;
			break;
		case 7: /* EI: takes effect after the next instruction */
			cpu->ei_delay = 2;
			break;
		default: /* unused opcodes lock up the CPU */
			cpu->hung = true;
			break;
		}
		break;

	case 4:
		if (y < 4) {
			call(cpu, cond(cpu, y));
		} else {
			cpu->hung = true;
		}
		break;

	case 5:
		if (!q) { /* PUSH rr */
			push16(cpu, *reg16_stack(cpu, p));
		} else if (p == 0) {
			call(cpu, true);
		} else {
			cpu->hung = true;
		}
		break;

	case 6:
		alu(cpu, (AluOp)y, fetch8(cpu));
		break;

	default: /* RST */
		push16(cpu, cpu->pc);
		cpu->pc = y * 8;
		break;
	}
}

static void trace_record(Cpu *cpu, uint16_t pc, uint8_t op) {
	cpu->trace_pc[cpu->trace_pos] = pc;
	cpu->trace_op[cpu->trace_pos] = op;
	cpu->trace_pos = (uint8_t)((cpu->trace_pos + 1) & (CPU_TRACE - 1));
}

static void exec(Cpu *cpu) {
	uint16_t start = cpu->pc;
	uint8_t op = fetch8(cpu);
	unsigned x = op >> 6;
	unsigned y = (op >> 3) & 7;
	unsigned z = op & 7;

	trace_record(cpu, start, op);

	switch (x) {
	case 0:
		exec_block0(cpu, y, z);
		break;
	case 1:
		if (y == OPERAND_HL_MEM && z == OPERAND_HL_MEM) {
			halt(cpu);
		} else {
			set8(cpu, y, get8(cpu, z));
		}
		break;
	case 2:
		alu(cpu, (AluOp)y, get8(cpu, z));
		break;
	default:
		exec_block3(cpu, y, z);
		break;
	}

	if (cpu->hung && !cpu->faulted) {
		cpu->faulted = true;
		cpu->fault_pc = start;
		cpu->fault_op = op;
	}
}

/* -- interrupts and top-level step -- */

/*
 * Interrupt dispatch takes 5 M-cycles. The vector is chosen *after* the high
 * byte of PC is pushed, so that push can cancel or redirect the interrupt by
 * overwriting IE (a real hardware quirk); if nothing is pending anymore PC=0.
 */
static void service_interrupt(Cpu *cpu) {
	cpu->ime = false;
	cpu->ei_delay = 0;
	idle_mcycle(cpu);
	idu_mcycle(cpu, cpu->sp);
	cpu->sp--;
	wr_idu(cpu, cpu->sp, cpu->pc >> 8);

	uint8_t pending = pending_irqs(cpu);
	cpu->sp--;
	wr(cpu, cpu->sp, cpu->pc & 0xFF);

	if (pending) {
		unsigned bit = 0;
		while (!((pending >> bit) & 1)) {
			bit++;
		}
		bus_write(cpu->gb, REG_IF, bus_read(cpu->gb, REG_IF) & ~(1u << bit));
		cpu->pc = (uint16_t)(IRQ_VECTOR_BASE + bit * IRQ_VECTOR_STEP);
	} else {
		cpu->pc = 0;
	}
	idle_mcycle(cpu);
}

void cpu_reset(Cpu *cpu, Gb *gb, bool has_boot_rom) {
	*cpu = (Cpu){0};
	cpu->gb = gb;
	if (has_boot_rom) {
		return; /* the boot ROM sets up the registers itself, starting at PC 0 */
	}

	/* Register values the DMG boot ROM leaves behind. */
	cpu->af = 0x01B0;
	cpu->bc = 0x0013;
	cpu->de = 0x00D8;
	cpu->hl = 0x014D;
	cpu->sp = 0xFFFE;
	cpu->pc = 0x0100;
}

/* Runs one instruction, one interrupt dispatch, or one idle M-cycle (halted/stopped/hung). */
void cpu_step(Cpu *cpu) {
	if (cpu->hung) {
		idle_mcycle(cpu);
		return;
	}

	uint8_t pending = pending_irqs(cpu);

	if (cpu->stopped) {
		if (!(bus_read(cpu->gb, REG_IF) & (1u << IRQ_JOYPAD))) {
			idle_mcycle(cpu);
			return;
		}
		cpu->stopped = false;
	}

	if (cpu->halted) {
		if (!pending) {
			idle_mcycle(cpu);
			return;
		}
		cpu->halted = false;
	}

	if (cpu->ime && pending) {
		service_interrupt(cpu);
		return;
	}

	exec(cpu);

	if (cpu->ei_delay && --cpu->ei_delay == 0) {
		cpu->ime = true;
	}
}

#include "cpu.h"

#include <stddef.h>

#include "bus.h"

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

#define ADDR_IF	 0xFF0F
#define ADDR_IE	 0xFFFF
#define IRQ_MASK 0x1F

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
	ACC_NONE,
	ACC_READ,
	ACC_WRITE,
} Access;

static uint8_t flags(bool z, bool n, bool h, bool cy) {
	return (z ? FLAG_Z : 0) | (n ? FLAG_N : 0) | (h ? FLAG_H : 0) | (cy ? FLAG_C : 0);
}

static void tick(Cpu *cpu) {
	gb_cycle_begin(cpu->gb);
	gb_tick(cpu->gb);
}

static bool in_oam(uint16_t addr) {
	return (addr >> 8) == 0xFE;
}

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

	gb_cycle_begin(cpu->gb);
	oam_bug(cpu, acc, addr, idu, idu_addr);
	if (acc == ACC_READ) {
		result = bus_read(cpu->gb, addr);
	} else if (acc == ACC_WRITE) {
		bus_write(cpu->gb, addr, value);
	}
	gb_tick(cpu->gb);
	return result;
}

static uint8_t rd(Cpu *cpu, uint16_t addr) {
	return mcycle(cpu, ACC_READ, addr, 0, false, 0);
}

static void wr(Cpu *cpu, uint16_t addr, uint8_t value) {
	mcycle(cpu, ACC_WRITE, addr, value, false, 0);
}

static uint8_t rd_idu(Cpu *cpu, uint16_t addr) {
	return mcycle(cpu, ACC_READ, addr, 0, true, addr);
}

static void wr_idu(Cpu *cpu, uint16_t addr, uint8_t value) {
	mcycle(cpu, ACC_WRITE, addr, value, true, addr);
}

static void idu_tick(Cpu *cpu, uint16_t value) {
	mcycle(cpu, ACC_NONE, 0, 0, true, value);
}

static uint8_t fetch8(Cpu *cpu) {
	uint8_t value;

	if (cpu->halt_bug) {
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
	idu_tick(cpu, cpu->sp);
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

static uint8_t *r8_ptr(Cpu *cpu, unsigned i) {
	uint8_t *table[8] = {&cpu->b, &cpu->c, &cpu->d, &cpu->e, &cpu->h, &cpu->l, NULL, &cpu->a};
	return table[i];
}

static uint8_t get8(Cpu *cpu, unsigned i) {
	return i == 6 ? rd(cpu, cpu->hl) : *r8_ptr(cpu, i);
}

static void set8(Cpu *cpu, unsigned i, uint8_t value) {
	if (i == 6) {
		wr(cpu, cpu->hl, value);
	} else {
		*r8_ptr(cpu, i) = value;
	}
}

static uint16_t *rp(Cpu *cpu, unsigned p) {
	uint16_t *table[4] = {&cpu->bc, &cpu->de, &cpu->hl, &cpu->sp};
	return table[p];
}

static bool cond(const Cpu *cpu, unsigned cc) {
	bool set = cpu->f & (cc < 2 ? FLAG_Z : FLAG_C);
	return (cc & 1) ? set : !set;
}

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
	tick(cpu);
}

static uint16_t sp_plus_e8(Cpu *cpu) {
	uint8_t e = fetch8(cpu);
	bool half = ((cpu->sp & 0xF) + (e & 0xF)) > 0xF;
	bool carry = ((cpu->sp & 0xFF) + e) > 0xFF;
	cpu->f = flags(false, false, half, carry);
	return (uint16_t)(cpu->sp + (int8_t)e);
}

static uint8_t shift(Cpu *cpu, unsigned op, uint8_t v) {
	unsigned old = (cpu->f & FLAG_C) ? 1 : 0;
	uint8_t r;
	bool cy;

	switch (op) {
	case 0:
		cy = v >> 7;
		r = (uint8_t)((v << 1) | cy);
		break;
	case 1:
		cy = v & 1;
		r = (uint8_t)((v >> 1) | (cy << 7));
		break;
	case 2:
		cy = v >> 7;
		r = (uint8_t)((v << 1) | old);
		break;
	case 3:
		cy = v & 1;
		r = (uint8_t)((v >> 1) | (old << 7));
		break;
	case 4:
		cy = v >> 7;
		r = (uint8_t)(v << 1);
		break;
	case 5:
		cy = v & 1;
		r = (uint8_t)((v >> 1) | (v & 0x80));
		break;
	case 6:
		cy = false;
		r = (uint8_t)((v << 4) | (v >> 4));
		break;
	default:
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

static void jr(Cpu *cpu, bool take) {
	int8_t e = (int8_t)fetch8(cpu);
	if (take) {
		tick(cpu);
		cpu->pc = (uint16_t)(cpu->pc + e);
	}
}

static void jp(Cpu *cpu, bool take) {
	uint16_t addr = fetch16(cpu);
	if (take) {
		tick(cpu);
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
		tick(cpu);
	}
}

static void halt(Cpu *cpu) {
	bool pending = bus_read(cpu->gb, ADDR_IE) & bus_read(cpu->gb, ADDR_IF) & IRQ_MASK;
	if (!cpu->ime && pending) {
		cpu->halt_bug = true;
	} else {
		cpu->halted = true;
	}
}

static void exec_cb(Cpu *cpu) {
	uint8_t op = fetch8(cpu);
	unsigned x = op >> 6;
	unsigned y = (op >> 3) & 7;
	unsigned z = op & 7;
	uint8_t v = get8(cpu, z);

	switch (x) {
	case 0:
		set8(cpu, z, shift(cpu, y, v));
		break;
	case 1:
		cpu->f = flags(!((v >> y) & 1), false, true, cpu->f & FLAG_C);
		break;
	case 2:
		set8(cpu, z, v & ~(1u << y));
		break;
	default:
		set8(cpu, z, v | (1u << y));
		break;
	}
}

static void exec_x0(Cpu *cpu, unsigned y, unsigned z) {
	unsigned p = y >> 1;
	bool q = y & 1;

	switch (z) {
	case 0:
		switch (y) {
		case 0:
			break;
		case 1: {
			uint16_t addr = fetch16(cpu);
			wr(cpu, addr, cpu->sp & 0xFF);
			wr(cpu, addr + 1, cpu->sp >> 8);
			break;
		}
		case 2:
			fetch8(cpu);
			bus_write(cpu->gb, 0xFF04, 0);
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
			add_hl(cpu, *rp(cpu, p));
		} else {
			*rp(cpu, p) = fetch16(cpu);
		}
		break;

	case 2: {
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

	case 3: {
		uint16_t *reg = rp(cpu, p);

		idu_tick(cpu, *reg);
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

	default:
		switch (y) {
		case 0:
		case 1:
		case 2:
		case 3:
			cpu->a = shift(cpu, y, cpu->a);
			cpu->f &= FLAG_C;
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

static void exec_x3(Cpu *cpu, unsigned y, unsigned z) {
	unsigned p = y >> 1;
	bool q = y & 1;

	switch (z) {
	case 0:
		switch (y) {
		case 4:
			wr(cpu, 0xFF00 | fetch8(cpu), cpu->a);
			break;
		case 5:
			cpu->sp = sp_plus_e8(cpu);
			tick(cpu);
			tick(cpu);
			break;
		case 6:
			cpu->a = rd(cpu, 0xFF00 | fetch8(cpu));
			break;
		case 7:
			cpu->hl = sp_plus_e8(cpu);
			tick(cpu);
			break;
		default:
			tick(cpu);
			ret(cpu, cond(cpu, y));
			break;
		}
		break;

	case 1:
		if (!q) {
			uint16_t *reg = (p == 3) ? &cpu->af : rp(cpu, p);
			*reg = pop16(cpu);
			cpu->f &= 0xF0;
		} else if (p == 0) {
			ret(cpu, true);
		} else if (p == 1) {
			ret(cpu, true);
			cpu->ime = true;
		} else if (p == 2) {
			cpu->pc = cpu->hl;
		} else {
			cpu->sp = cpu->hl;
			tick(cpu);
		}
		break;

	case 2:
		switch (y) {
		case 4:
			wr(cpu, 0xFF00 | cpu->c, cpu->a);
			break;
		case 5:
			wr(cpu, fetch16(cpu), cpu->a);
			break;
		case 6:
			cpu->a = rd(cpu, 0xFF00 | cpu->c);
			break;
		case 7:
			cpu->a = rd(cpu, fetch16(cpu));
			break;
		default:
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
		case 6:
			cpu->ime = false;
			cpu->ei_delay = 0;
			break;
		case 7:
			cpu->ei_delay = 2;
			break;
		default:
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
		if (!q) {
			uint16_t value = (p == 3) ? cpu->af : *rp(cpu, p);
			push16(cpu, value);
		} else if (p == 0) {
			call(cpu, true);
		} else {
			cpu->hung = true;
		}
		break;

	case 6:
		alu(cpu, y, fetch8(cpu));
		break;

	default:
		push16(cpu, cpu->pc);
		cpu->pc = y * 8;
		break;
	}
}

static void exec(Cpu *cpu) {
	uint8_t op = fetch8(cpu);
	unsigned x = op >> 6;
	unsigned y = (op >> 3) & 7;
	unsigned z = op & 7;

	switch (x) {
	case 0:
		exec_x0(cpu, y, z);
		break;
	case 1:
		if (y == 6 && z == 6) {
			halt(cpu);
		} else {
			set8(cpu, y, get8(cpu, z));
		}
		break;
	case 2:
		alu(cpu, y, get8(cpu, z));
		break;
	default:
		exec_x3(cpu, y, z);
		break;
	}
}

static void service_interrupt(Cpu *cpu) {
	cpu->ime = false;
	cpu->ei_delay = 0;
	tick(cpu);
	idu_tick(cpu, cpu->sp);
	cpu->sp--;
	wr_idu(cpu, cpu->sp, cpu->pc >> 8);

	uint8_t pending = bus_read(cpu->gb, ADDR_IE) & bus_read(cpu->gb, ADDR_IF) & IRQ_MASK;
	cpu->sp--;
	wr(cpu, cpu->sp, cpu->pc & 0xFF);

	if (pending) {
		unsigned bit = 0;
		while (!((pending >> bit) & 1)) {
			bit++;
		}
		bus_write(cpu->gb, ADDR_IF, bus_read(cpu->gb, ADDR_IF) & ~(1u << bit));
		cpu->pc = (uint16_t)(0x40 + bit * 8);
	} else {
		cpu->pc = 0;
	}
	tick(cpu);
}

void cpu_reset(Cpu *cpu, Gb *gb, bool boot) {
	*cpu = (Cpu){0};
	cpu->gb = gb;
	if (boot) {
		return;
	}

	cpu->af = 0x01B0;
	cpu->bc = 0x0013;
	cpu->de = 0x00D8;
	cpu->hl = 0x014D;
	cpu->sp = 0xFFFE;
	cpu->pc = 0x0100;
}

void cpu_step(Cpu *cpu) {
	if (cpu->hung) {
		tick(cpu);
		return;
	}

	uint8_t pending = bus_read(cpu->gb, ADDR_IE) & bus_read(cpu->gb, ADDR_IF) & IRQ_MASK;

	if (cpu->stopped) {
		if (!(bus_read(cpu->gb, ADDR_IF) & 0x10)) {
			tick(cpu);
			return;
		}
		cpu->stopped = false;
	}

	if (cpu->halted) {
		if (!pending) {
			tick(cpu);
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

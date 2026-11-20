#ifndef GBOYC_CPU_H
#define GBOYC_CPU_H

#include <stdbool.h>
#include <stdint.h>

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "cpu.h requires a little-endian host"
#endif

#define CPU_TRACE 16

#define CPU_REG_PAIR(hi, lo, pair)                                                                 \
	union {                                                                                        \
		struct {                                                                                   \
			uint8_t lo;                                                                            \
			uint8_t hi;                                                                            \
		};                                                                                         \
		uint16_t pair;                                                                             \
	}

typedef struct Gb Gb;

typedef struct {
	CPU_REG_PAIR(a, f, af);
	CPU_REG_PAIR(b, c, bc);
	CPU_REG_PAIR(d, e, de);
	CPU_REG_PAIR(h, l, hl);
	uint16_t sp;
	uint16_t pc;
	bool ime;
	bool halted;
	bool halt_bug;
	bool hung;
	bool stopped;
	uint8_t ei_delay;
	bool faulted;
	uint8_t fault_op;
	uint16_t fault_pc;
	uint8_t trace_pos;
	uint8_t trace_op[CPU_TRACE];
	uint16_t trace_pc[CPU_TRACE];
	Gb *gb;
} Cpu;

void cpu_reset(Cpu *cpu, Gb *gb, bool boot);
void cpu_step(Cpu *cpu);

#endif

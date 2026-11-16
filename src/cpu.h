#ifndef GBOYC_CPU_H
#define GBOYC_CPU_H

#include <stdbool.h>
#include <stdint.h>

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "cpu.h requires a little-endian host"
#endif

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
	Gb *gb;
} Cpu;

void cpu_reset(Cpu *cpu, Gb *gb, bool boot);
void cpu_step(Cpu *cpu);

#endif

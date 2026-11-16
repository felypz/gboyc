#include "timer.h"

#define ADDR_DIV  0xFF04
#define ADDR_TIMA 0xFF05
#define ADDR_TMA  0xFF06
#define ADDR_TAC  0xFF07

static const uint8_t TAC_BIT[4] = {9, 3, 5, 7};

static bool signal(const Timer *t) {
	return (t->tac & 4) && ((t->div >> TAC_BIT[t->tac & 3]) & 1);
}

static bool increment(Timer *t) {
	if (++t->tima == 0) {
		t->tima = t->tma;
		return true;
	}
	return false;
}

static bool update(Timer *t, uint16_t div, uint8_t tac) {
	bool before = signal(t);
	t->div = div;
	t->tac = tac;
	return before && !signal(t) && increment(t);
}

void timer_reset(Timer *t) {
	*t = (Timer){.div = 0xABCC};
}

bool timer_step(Timer *t, int cycles) {
	bool irq = false;
	for (int i = 0; i < cycles; i += 4) {
		irq |= update(t, (uint16_t)(t->div + 4), t->tac);
	}
	return irq;
}

uint8_t timer_read(const Timer *t, uint16_t addr) {
	switch (addr) {
	case ADDR_DIV:
		return t->div >> 8;
	case ADDR_TIMA:
		return t->tima;
	case ADDR_TMA:
		return t->tma;
	default:
		return t->tac | 0xF8;
	}
}

bool timer_write(Timer *t, uint16_t addr, uint8_t value) {
	switch (addr) {
	case ADDR_DIV:
		return update(t, 0, t->tac);
	case ADDR_TIMA:
		t->tima = value;
		return false;
	case ADDR_TMA:
		t->tma = value;
		return false;
	default:
		return update(t, t->div, value & 7);
	}
}

#include "timer.h"

#define ADDR_DIV  0xFF04
#define ADDR_TIMA 0xFF05
#define ADDR_TMA  0xFF06
#define ADDR_TAC  0xFF07

enum {
	RELOAD_NONE,
	RELOAD_PENDING,
	RELOAD_DONE,
};

static const uint8_t TAC_BIT[4] = {9, 3, 5, 7};

static bool signal(const Timer *t) {
	return (t->tac & 4) && ((t->div >> TAC_BIT[t->tac & 3]) & 1);
}

static void increment(Timer *t) {
	if (++t->tima == 0) {
		t->reload = RELOAD_PENDING;
	}
}

static void update(Timer *t, uint16_t div, uint8_t tac) {
	bool before = signal(t);

	t->div = div;
	t->tac = tac;
	if (before && !signal(t)) {
		increment(t);
	}
}

void timer_reset(Timer *t) {
	*t = (Timer){.div = 0xABCC};
}

bool timer_step(Timer *t) {
	bool irq = false;

	if (t->reload == RELOAD_PENDING) {
		t->tima = t->tma;
		t->reload = RELOAD_DONE;
		irq = true;
	} else {
		t->reload = RELOAD_NONE;
	}

	update(t, (uint16_t)(t->div + 4), t->tac);
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

void timer_write(Timer *t, uint16_t addr, uint8_t value) {
	switch (addr) {
	case ADDR_DIV:
		update(t, 0, t->tac);
		break;
	case ADDR_TIMA:
		if (t->reload != RELOAD_DONE) {
			t->tima = value;
			t->reload = RELOAD_NONE;
		}
		break;
	case ADDR_TMA:
		t->tma = value;
		if (t->reload == RELOAD_DONE) {
			t->tima = value;
		}
		break;
	default:
		update(t, t->div, value & 7);
		break;
	}
}

#include "timer.h"

#include "memmap.h"

#define TAC_ENABLE 0x04
#define TAC_MASK   0x07

/* DIV bit that clocks TIMA for each TAC frequency select (4096/262144/65536/16384 Hz). */
static const uint8_t TAC_DIV_BIT[4] = {9, 3, 5, 7};

/* TIMA increments on the falling edge of this signal. */
static bool selected_bit_high(const Timer *t) {
	return (t->tac & TAC_ENABLE) && ((t->div >> TAC_DIV_BIT[t->tac & 3]) & 1);
}

static void increment_tima(Timer *t) {
	if (++t->tima == 0) {
		t->reload = TIMER_RELOAD_PENDING;
	}
}

/* DIV and TAC writes can both cause a falling edge, so they share one path. */
static void timer_set_div_tac(Timer *t, uint16_t div, uint8_t tac) {
	bool before = selected_bit_high(t);

	t->div = div;
	t->tac = tac;
	if (before && !selected_bit_high(t)) {
		increment_tima(t);
	}
}

void timer_reset(Timer *t, bool has_boot_rom) {
	*t = (Timer){0};
	if (!has_boot_rom) {
		t->div = 0xABCC; /* value after the DMG boot ROM */
	}
}

bool timer_step(Timer *t) {
	bool irq = false;

	if (t->reload == TIMER_RELOAD_PENDING) {
		t->tima = t->tma;
		t->reload = TIMER_RELOAD_DONE;
		irq = true;
	} else {
		t->reload = TIMER_RELOAD_IDLE;
	}

	timer_set_div_tac(t, (uint16_t)(t->div + 4), t->tac);
	return irq;
}

uint8_t timer_read(const Timer *t, uint16_t addr) {
	switch (addr) {
	case REG_DIV:
		return t->div >> 8;
	case REG_TIMA:
		return t->tima;
	case REG_TMA:
		return t->tma;
	default:
		return t->tac | 0xF8;
	}
}

void timer_write(Timer *t, uint16_t addr, uint8_t value) {
	switch (addr) {
	case REG_DIV:
		timer_set_div_tac(t, 0, t->tac);
		break;
	case REG_TIMA:
		if (t->reload != TIMER_RELOAD_DONE) {
			t->tima = value;
			t->reload = TIMER_RELOAD_IDLE;
		}
		break;
	case REG_TMA:
		t->tma = value;
		if (t->reload == TIMER_RELOAD_DONE) {
			t->tima = value;
		}
		break;
	default:
		timer_set_div_tac(t, t->div, value & TAC_MASK);
		break;
	}
}

bool timer_state_sane(const Timer *t) {
	return t->reload <= TIMER_RELOAD_DONE && (t->tac & ~TAC_MASK) == 0;
}

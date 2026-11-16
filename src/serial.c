#include "serial.h"

#include <stdio.h>

#define ADDR_SB	   0xFF01
#define BIT_CYCLES 512

void serial_reset(Serial *s) {
	*s = (Serial){0};
}

bool serial_step(Serial *s) {
	if (!s->active) {
		return false;
	}

	s->counter += 4;
	if (s->counter < BIT_CYCLES) {
		return false;
	}

	s->counter = 0;
	s->sb = (uint8_t)((s->sb << 1) | 1);
	if (++s->bits < 8) {
		return false;
	}

	s->active = false;
	s->sc &= 0x7F;
	putchar(s->out);
	fflush(stdout);
	return true;
}

uint8_t serial_read(const Serial *s, uint16_t addr) {
	return addr == ADDR_SB ? s->sb : (uint8_t)(s->sc | 0x7E);
}

void serial_write(Serial *s, uint16_t addr, uint8_t value) {
	if (addr == ADDR_SB) {
		s->sb = value;
		return;
	}

	s->sc = value & 0x81;
	s->active = (s->sc == 0x81);
	if (s->active) {
		s->counter = 0;
		s->bits = 0;
		s->out = s->sb;
	}
}

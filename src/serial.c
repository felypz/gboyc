#include "serial.h"

#include <stdio.h>

#include "memmap.h"
#include "timing.h"

#define SC_TRANSFER_START	0x80
#define SC_INTERNAL_CLOCK	0x01
#define SC_WRITABLE			(SC_TRANSFER_START | SC_INTERNAL_CLOCK)
#define SC_UNUSED_BITS		0x7E
#define SERIAL_DOTS_PER_BIT 512 /* internal clock: 8192 Hz */

void serial_reset(Serial *s) {
	*s = (Serial){0};
}

bool serial_step(Serial *s) {
	if (!s->active) {
		return false;
	}

	s->counter += GB_DOTS_PER_MCYCLE;
	if (s->counter < SERIAL_DOTS_PER_BIT) {
		return false;
	}

	s->counter = 0;
	s->sb = (uint8_t)((s->sb << 1) | 1); /* no link partner: the line reads as 1 */
	if (++s->bits < 8) {
		return false;
	}

	s->active = false;
	s->sc &= (uint8_t)~SC_TRANSFER_START;

	/* Test ROMs print their results through the serial port. */
	if (!s->suppress_output) {
		putchar(s->out);
		fflush(stdout);
	}
	return true;
}

uint8_t serial_read(const Serial *s, uint16_t addr) {
	return addr == REG_SB ? s->sb : (uint8_t)(s->sc | SC_UNUSED_BITS);
}

void serial_write(Serial *s, uint16_t addr, uint8_t value) {
	if (addr == REG_SB) {
		s->sb = value;
		return;
	}

	s->sc = value & SC_WRITABLE;
	s->active = (s->sc == SC_WRITABLE); /* only the internal clock is emulated */
	if (s->active) {
		s->counter = 0;
		s->bits = 0;
		s->out = s->sb;
	}
}

#ifndef GBOYC_SERIAL_H
#define GBOYC_SERIAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint8_t sb;
	uint8_t sc;
	uint8_t out;
	uint8_t bits;
	uint16_t counter;
	bool active;
	bool suppress_output; /* set while the stepper re-executes an instruction, to avoid double
							 prints */
} Serial;

void serial_reset(Serial *s);
/* Advances one M-cycle. Returns true if a transfer finished and raised an interrupt. */
bool serial_step(Serial *s);
uint8_t serial_read(const Serial *s, uint16_t addr);
void serial_write(Serial *s, uint16_t addr, uint8_t value);

#endif

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
	bool quiet;
} Serial;

void serial_reset(Serial *s);
bool serial_step(Serial *s);
uint8_t serial_read(const Serial *s, uint16_t addr);
void serial_write(Serial *s, uint16_t addr, uint8_t value);

#endif

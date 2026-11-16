#ifndef GBOYC_TIMER_H
#define GBOYC_TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint16_t div;
	uint8_t tima;
	uint8_t tma;
	uint8_t tac;
	uint8_t reload;
} Timer;

void timer_reset(Timer *t);
bool timer_step(Timer *t);
uint8_t timer_read(const Timer *t, uint16_t addr);
void timer_write(Timer *t, uint16_t addr, uint8_t value);

#endif

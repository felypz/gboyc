#ifndef GBOYC_TIMER_H
#define GBOYC_TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	TIMER_RELOAD_IDLE,
	TIMER_RELOAD_PENDING,
	TIMER_RELOAD_DONE,
} TimerReload;

typedef struct {
	uint16_t div;
	uint8_t tima;
	uint8_t tma;
	uint8_t tac;
	uint8_t reload; /* TimerReload */
} Timer;

void timer_reset(Timer *t, bool has_boot_rom);
/* Advances one M-cycle. Returns true if TIMA overflowed and raised an interrupt. */
bool timer_step(Timer *t);
uint8_t timer_read(const Timer *t, uint16_t addr);
void timer_write(Timer *t, uint16_t addr, uint8_t value);
bool timer_state_sane(const Timer *t);

#endif

#ifndef GBOYC_STEPPER_H
#define GBOYC_STEPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gb.h"

typedef struct {
	Gb *gb_snapshot;
	uint8_t *cart_snapshot;
	size_t cart_size;
	unsigned cycles;
	bool pending;
} Stepper;

void stepper_free(Stepper *s);
void stepper_discard(Stepper *s);
void stepper_commit(Stepper *s, Gb *gb);
void stepper_frame(Stepper *s, Gb *gb);
void stepper_instruction(Stepper *s, Gb *gb);
void stepper_cycle(Stepper *s, Gb *gb);

#endif

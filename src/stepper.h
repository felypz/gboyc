#ifndef GBOYC_STEPPER_H
#define GBOYC_STEPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gb.h"

/*
 * Debug stepping (frame / instruction / M-cycle). A snapshot of the machine is
 * kept so a partially executed instruction can be rolled back and replayed
 * up to the requested M-cycle.
 */
typedef struct {
	Gb *gb_snapshot;
	uint8_t *cart_snapshot;
	size_t cart_size;
	unsigned mcycles; /* M-cycles already executed in the pending instruction */
	bool pending;
} Stepper;

void stepper_free(Stepper *s);
void stepper_discard(Stepper *s);
void stepper_commit(Stepper *s, Gb *gb);
/* Returns false if the guest stalled (no frame within the step budget). */
bool stepper_frame(Stepper *s, Gb *gb);
void stepper_instruction(Stepper *s, Gb *gb);
void stepper_mcycle(Stepper *s, Gb *gb);

#endif

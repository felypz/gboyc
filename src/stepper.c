#include "stepper.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

void stepper_free(Stepper *s) {
	free(s->gb_snapshot);
	free(s->cart_snapshot);
	*s = (Stepper){0};
}

void stepper_discard(Stepper *s) {
	s->pending = false;
	s->mcycles = 0;
}

static bool snapshot_take(Stepper *s, const Gb *gb) {
	size_t cart_size = cartridge_state_size(gb->cart);

	if (!s->gb_snapshot) {
		s->gb_snapshot = malloc(sizeof *s->gb_snapshot);
	}
	if (s->cart_size != cart_size) {
		free(s->cart_snapshot);
		s->cart_snapshot = malloc(cart_size);
		s->cart_size = cart_size;
	}
	if (!s->gb_snapshot || !s->cart_snapshot) {
		return false;
	}

	memcpy(s->gb_snapshot, gb, sizeof *gb);
	cartridge_state_write(gb->cart, s->cart_snapshot);
	return true;
}

static void snapshot_restore(const Stepper *s, Gb *gb) {
	memcpy(gb, s->gb_snapshot, sizeof *gb);
	/* The snapshot was written by snapshot_take(), so it always passes validation. */
	cartridge_state_restore(gb->cart, s->cart_snapshot, s->cart_size);
}

/* Runs one instruction, stopping early after max_mcycles. Returns true if it completed. */
static bool run_limited(Gb *gb, unsigned max_mcycles) {
	/* Local on purpose: gb->step.escape is cleared on both exits, so it never dangles. */
	jmp_buf escape;

	gb->step.escape = &escape;
	gb->step.max_mcycles = max_mcycles;
	gb->step.mcycles_done = 0;

	if (setjmp(escape)) {
		gb->step.escape = NULL;
		return false;
	}

	cpu_step(&gb->cpu);
	gb->step.escape = NULL;
	return true;
}

/* Finishes a partially stepped instruction by replaying it from the snapshot, unbounded. */
void stepper_commit(Stepper *s, Gb *gb) {
	if (!s->pending) {
		return;
	}

	snapshot_restore(s, gb);
	cpu_step(&gb->cpu);
	stepper_discard(s);
}

bool stepper_frame(Stepper *s, Gb *gb) {
	stepper_commit(s, gb);
	ppu_take_frame(&gb->ppu); /* drop a frame that was already completed before stepping */
	return gb_run_frame(gb);
}

void stepper_instruction(Stepper *s, Gb *gb) {
	if (s->pending) {
		stepper_commit(s, gb);
		return;
	}
	cpu_step(&gb->cpu);
}

/*
 * Advances exactly one more M-cycle into the current instruction. Each call
 * rolls back to the snapshot and replays one M-cycle further than the last.
 */
void stepper_mcycle(Stepper *s, Gb *gb) {
	if (!s->pending) {
		if (!snapshot_take(s, gb)) {
			return;
		}
		s->pending = true;
		s->mcycles = 0;
	} else {
		snapshot_restore(s, gb);
	}

	/* The instruction is replayed many times; print serial output only once, at commit. */
	gb->serial.suppress_output = true;
	bool completed = run_limited(gb, s->mcycles + 1);
	gb->serial.suppress_output = false;

	if (completed) {
		stepper_commit(s, gb);
	} else {
		s->mcycles++;
	}
}

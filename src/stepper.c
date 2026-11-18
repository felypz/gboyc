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
	s->cycles = 0;
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
	cartridge_state_read(gb->cart, s->cart_snapshot, s->cart_size);
}

static bool run_limited(Gb *gb, unsigned limit) {
	jmp_buf buf;

	gb->step.abort = &buf;
	gb->step.limit = limit;
	gb->step.count = 0;

	if (setjmp(buf)) {
		gb->step.abort = NULL;
		return false;
	}

	cpu_step(&gb->cpu);
	gb->step.abort = NULL;
	return true;
}

void stepper_commit(Stepper *s, Gb *gb) {
	if (!s->pending) {
		return;
	}

	snapshot_restore(s, gb);
	cpu_step(&gb->cpu);
	stepper_discard(s);
}

void stepper_frame(Stepper *s, Gb *gb) {
	stepper_commit(s, gb);
	ppu_take_frame(&gb->ppu);
	gb_run_frame(gb);
}

void stepper_instruction(Stepper *s, Gb *gb) {
	if (s->pending) {
		stepper_commit(s, gb);
		return;
	}
	cpu_step(&gb->cpu);
}

void stepper_cycle(Stepper *s, Gb *gb) {
	if (!s->pending) {
		if (!snapshot_take(s, gb)) {
			return;
		}
		s->pending = true;
		s->cycles = 0;
	} else {
		snapshot_restore(s, gb);
	}

	gb->serial.quiet = true;
	bool done = run_limited(gb, s->cycles + 1);
	gb->serial.quiet = false;

	if (done) {
		snapshot_restore(s, gb);
		cpu_step(&gb->cpu);
		stepper_discard(s);
	} else {
		s->cycles++;
	}
}

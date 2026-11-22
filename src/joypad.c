#include "joypad.h"

/* JOYP bits 4-5 are active low: a cleared bit selects that group of buttons. */
#define JOYP_SELECT_DPAD	0x10
#define JOYP_SELECT_BUTTONS 0x20
#define JOYP_SELECT_BITS	(JOYP_SELECT_DPAD | JOYP_SELECT_BUTTONS)
#define JOYP_UNUSED_BITS	0xC0
#define JOYP_INPUT_BITS		0x0F

/* The pressed mask holds the d-pad in its low nibble and the buttons in its high nibble. */
_Static_assert(JOY_BTN_RIGHT == 0 && JOY_BTN_DOWN == 3, "d-pad must occupy the low nibble");
_Static_assert(JOY_BTN_A == 4 && JOY_BTN_START == 7, "buttons must occupy the high nibble");

void joypad_reset(Joypad *j) {
	*j = (Joypad){.select_lines = JOYP_SELECT_BITS};
}

uint8_t joypad_read(const Joypad *j) {
	uint8_t pressed = 0;

	if (!(j->select_lines & JOYP_SELECT_DPAD)) {
		pressed |= j->mask & JOYP_INPUT_BITS;
	}
	if (!(j->select_lines & JOYP_SELECT_BUTTONS)) {
		pressed |= j->mask >> 4;
	}
	return JOYP_UNUSED_BITS | j->select_lines | (~pressed & JOYP_INPUT_BITS);
}

/* The joypad interrupt fires when any input line goes from high to low. */
static bool joypad_press_edge(uint8_t before, uint8_t after) {
	return (before & ~after & JOYP_INPUT_BITS) != 0;
}

bool joypad_write(Joypad *j, uint8_t value) {
	uint8_t before = joypad_read(j);

	j->select_lines = value & JOYP_SELECT_BITS;
	return joypad_press_edge(before, joypad_read(j));
}

bool joypad_set(Joypad *j, uint8_t mask) {
	uint8_t before = joypad_read(j);

	j->mask = mask;
	return joypad_press_edge(before, joypad_read(j));
}

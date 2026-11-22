#ifndef GBOYC_JOYPAD_H
#define GBOYC_JOYPAD_H

#include <stdbool.h>
#include <stdint.h>

/* Button order is also the bit position in the mask passed to joypad_set(). */
typedef enum {
	JOY_BTN_RIGHT,
	JOY_BTN_LEFT,
	JOY_BTN_UP,
	JOY_BTN_DOWN,
	JOY_BTN_A,
	JOY_BTN_B,
	JOY_BTN_SELECT,
	JOY_BTN_START,
	JOY_BTN_COUNT,
} JoyButton;

#define JOY_MASK(button) ((uint8_t)(1u << (button)))

typedef struct {
	uint8_t select_lines; /* bits 4-5 of JOYP as last written */
	uint8_t mask;		  /* pressed buttons, JOY_MASK() bits */
} Joypad;

void joypad_reset(Joypad *j);
uint8_t joypad_read(const Joypad *j);
/* Returns true if the write caused a joypad interrupt. */
bool joypad_write(Joypad *j, uint8_t value);
/* Returns true if the new mask caused a joypad interrupt. */
bool joypad_set(Joypad *j, uint8_t mask);

#endif

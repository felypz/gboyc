#ifndef GBOYC_JOYPAD_H
#define GBOYC_JOYPAD_H

#include <stdbool.h>
#include <stdint.h>

#define JOY_RIGHT  0x01
#define JOY_LEFT   0x02
#define JOY_UP	   0x04
#define JOY_DOWN   0x08
#define JOY_A	   0x10
#define JOY_B	   0x20
#define JOY_SELECT 0x40
#define JOY_START  0x80

typedef struct {
	uint8_t select;
	uint8_t mask;
} Joypad;

void joypad_reset(Joypad *j);
uint8_t joypad_read(const Joypad *j);
bool joypad_write(Joypad *j, uint8_t value);
bool joypad_set(Joypad *j, uint8_t mask);

#endif

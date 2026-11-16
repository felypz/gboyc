#include "joypad.h"

void joypad_reset(Joypad *j) {
	*j = (Joypad){.select = 0x30};
}

uint8_t joypad_read(const Joypad *j) {
	uint8_t pressed = 0;

	if (!(j->select & 0x10)) {
		pressed |= j->mask & 0x0F;
	}
	if (!(j->select & 0x20)) {
		pressed |= j->mask >> 4;
	}
	return 0xC0 | j->select | (~pressed & 0x0F);
}

bool joypad_write(Joypad *j, uint8_t value) {
	uint8_t before = joypad_read(j);
	j->select = value & 0x30;
	return before & ~joypad_read(j) & 0x0F;
}

bool joypad_set(Joypad *j, uint8_t mask) {
	uint8_t before = joypad_read(j);
	j->mask = mask;
	return before & ~joypad_read(j) & 0x0F;
}

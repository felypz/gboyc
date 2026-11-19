#include "bindings.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"

typedef struct {
	const char *label;
	const char *config;
} Name;

static const Name PAD[JOY_BUTTONS] = {
	[0] = {"Right", "right"},	[1] = {"Left", "left"},	  [2] = {"Up", "up"},
	[3] = {"Down", "down"},		[4] = {"A", "a"},		  [5] = {"B", "b"},
	[6] = {"Select", "select"}, [7] = {"Start", "start"},
};

static const Name SHORTCUT[SC_COUNT] = {
	[SC_PAUSE] = {"Pause", "pause"},
	[SC_SPEED] = {"Speed 2x", "speed"},
	[SC_RESET] = {"Reset", "reset"},
	[SC_SAVE_STATE] = {"Save state", "save"},
	[SC_LOAD_STATE] = {"Load state", "load"},
	[SC_STEP_FRAME] = {"Step frame", "step_frame"},
	[SC_STEP_INSTR] = {"Step instr.", "step_instr"},
	[SC_STEP_CYCLE] = {"Step cycle", "step_cycle"},
};

const char *bindings_pad_label(int button) {
	return PAD[button].label;
}

const char *bindings_shortcut_label(ShortcutAction action) {
	return SHORTCUT[action].label;
}

int bindings_shortcut_for_key(const Bindings *b, int scancode) {
	if (scancode <= 0) {
		return -1;
	}

	for (int i = 0; i < SC_COUNT; i++) {
		if (b->shortcut[i] == scancode) {
			return i;
		}
	}
	return -1;
}

int bindings_get(const Bindings *b, BindKind kind, int index, int slot) {
	return kind == BIND_PAD ? b->pad[index][slot] : b->shortcut[index];
}

void bindings_set(Bindings *b, BindKind kind, int index, int slot, int scancode) {
	if (kind == BIND_PAD) {
		b->pad[index][slot] = scancode;
	} else {
		b->shortcut[index] = scancode;
	}
}

bool bindings_conflict(const Bindings *b, BindKind kind, int index, int slot, int scancode,
					   char *msg, size_t size) {
	if (platform_key_reserved(scancode)) {
		snprintf(msg, size, "Reserved: state slots");
		return true;
	}

	int sc = bindings_shortcut_for_key(b, scancode);

	if (sc >= 0 && !(kind == BIND_SHORTCUT && sc == index)) {
		snprintf(msg, size, "%s%s", kind == BIND_PAD ? "Used by shortcut: " : "Already bound to ",
				 SHORTCUT[sc].label);
		return true;
	}

	for (int btn = 0; btn < JOY_BUTTONS; btn++) {
		for (int s = 0; s < BIND_KEY_SLOTS; s++) {
			bool same = kind == BIND_PAD && btn == index && s == slot;

			if (b->pad[btn][s] == scancode && !same) {
				snprintf(
					msg, size, "%s%s",
					kind == BIND_PAD ? "Already bound to " : "Used by control: ", PAD[btn].label);
				return true;
			}
		}
	}
	return false;
}

void bindings_write(const Bindings *b, FILE *f) {
	for (int i = 0; i < JOY_BUTTONS; i++) {
		fprintf(f, "joy.%s=%d,%d\n", PAD[i].config, b->pad[i][0], b->pad[i][1]);
	}
	for (int i = 0; i < SC_COUNT; i++) {
		fprintf(f, "key.%s=%d\n", SHORTCUT[i].config, b->shortcut[i]);
	}
}

bool bindings_parse(Bindings *b, const char *name, const char *value) {
	if (strncmp(name, "joy.", 4) == 0) {
		int first;
		int second;

		if (sscanf(value, "%d,%d", &first, &second) != 2 || !platform_key_valid(first) ||
			!platform_key_valid(second)) {
			return true;
		}
		for (int i = 0; i < JOY_BUTTONS; i++) {
			if (strcmp(name + 4, PAD[i].config) == 0) {
				b->pad[i][0] = first;
				b->pad[i][1] = second;
			}
		}
		return true;
	}

	if (strncmp(name, "key.", 4) == 0) {
		int key;

		if (sscanf(value, "%d", &key) != 1 || !platform_key_valid(key)) {
			return true;
		}
		for (int i = 0; i < SC_COUNT; i++) {
			if (strcmp(name + 4, SHORTCUT[i].config) == 0) {
				b->shortcut[i] = key;
			}
		}
		return true;
	}
	return false;
}

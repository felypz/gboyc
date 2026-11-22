#include "bindings.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"

#define CONFIG_PAD_PREFIX	   "joy."
#define CONFIG_SHORTCUT_PREFIX "key."
#define PREFIX_LEN			   4

typedef struct {
	const char *label;	/* shown in the UI */
	const char *config; /* key in settings.cfg, after the prefix */
} Name;

static const Name PAD_NAMES[JOY_BTN_COUNT] = {
	[JOY_BTN_RIGHT] = {"Right", "right"},
	[JOY_BTN_LEFT] = {"Left", "left"},
	[JOY_BTN_UP] = {"Up", "up"},
	[JOY_BTN_DOWN] = {"Down", "down"},
	[JOY_BTN_A] = {"A", "a"},
	[JOY_BTN_B] = {"B", "b"},
	[JOY_BTN_SELECT] = {"Select", "select"},
	[JOY_BTN_START] = {"Start", "start"},
};

static const Name SHORTCUT_NAMES[SC_COUNT] = {
	[SC_PAUSE] = {"Pause", "pause"},
	[SC_SPEED] = {"Speed 2x", "speed"},
	[SC_RESET] = {"Reset", "reset"},
	[SC_SAVE_STATE] = {"Save state", "save"},
	[SC_LOAD_STATE] = {"Load state", "load"},
	[SC_STEP_FRAME] = {"Step frame", "step_frame"},
	[SC_STEP_INSTR] = {"Step instr.", "step_instr"},
	[SC_STEP_MCYCLE] = {"Step M-cycle", "step_mcycle"},
};

const char *bindings_pad_label(JoyButton button) {
	return PAD_NAMES[button].label;
}

const char *bindings_shortcut_label(ShortcutAction action) {
	return SHORTCUT_NAMES[action].label;
}

ShortcutAction bindings_shortcut_for_key(const Bindings *b, int scancode) {
	if (scancode == KEY_NONE) {
		return SC_NONE;
	}

	for (int i = 0; i < SC_COUNT; i++) {
		if (b->shortcut[i] == scancode) {
			return (ShortcutAction)i;
		}
	}
	return SC_NONE;
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

	ShortcutAction other = bindings_shortcut_for_key(b, scancode);

	if (other != SC_NONE && !(kind == BIND_SHORTCUT && (int)other == index)) {
		snprintf(msg, size, "%s%s", kind == BIND_PAD ? "Used by shortcut: " : "Already bound to ",
				 SHORTCUT_NAMES[other].label);
		return true;
	}

	for (int button = 0; button < JOY_BTN_COUNT; button++) {
		for (int s = 0; s < BIND_KEY_SLOTS; s++) {
			bool is_self = kind == BIND_PAD && button == index && s == slot;

			if (b->pad[button][s] == scancode && !is_self) {
				snprintf(msg, size, "%s%s",
						 kind == BIND_PAD ? "Already bound to " : "Used by control: ",
						 PAD_NAMES[button].label);
				return true;
			}
		}
	}
	return false;
}

void bindings_write(const Bindings *b, FILE *f) {
	for (int i = 0; i < JOY_BTN_COUNT; i++) {
		fprintf(f, CONFIG_PAD_PREFIX "%s=%d,%d\n", PAD_NAMES[i].config, b->pad[i][0], b->pad[i][1]);
	}
	for (int i = 0; i < SC_COUNT; i++) {
		fprintf(f, CONFIG_SHORTCUT_PREFIX "%s=%d\n", SHORTCUT_NAMES[i].config, b->shortcut[i]);
	}
}

static void parse_pad(Bindings *b, const char *config, const char *value) {
	int first;
	int second;

	if (sscanf(value, "%d,%d", &first, &second) != 2 || !platform_key_valid(first) ||
		!platform_key_valid(second)) {
		return;
	}
	for (int i = 0; i < JOY_BTN_COUNT; i++) {
		if (strcmp(config, PAD_NAMES[i].config) == 0) {
			b->pad[i][0] = first;
			b->pad[i][1] = second;
		}
	}
}

static void parse_shortcut(Bindings *b, const char *config, const char *value) {
	int key;

	if (sscanf(value, "%d", &key) != 1 || !platform_key_valid(key)) {
		return;
	}
	for (int i = 0; i < SC_COUNT; i++) {
		if (strcmp(config, SHORTCUT_NAMES[i].config) == 0) {
			b->shortcut[i] = key;
		}
	}
}

void bindings_parse(Bindings *b, const char *name, const char *value) {
	if (strncmp(name, CONFIG_PAD_PREFIX, PREFIX_LEN) == 0) {
		parse_pad(b, name + PREFIX_LEN, value);
	} else if (strncmp(name, CONFIG_SHORTCUT_PREFIX, PREFIX_LEN) == 0) {
		parse_shortcut(b, name + PREFIX_LEN, value);
	}
}

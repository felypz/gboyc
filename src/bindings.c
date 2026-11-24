#include "bindings.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"

#define CONFIG_PAD_PREFIX	   "joy"
#define CONFIG_SHORTCUT_PREFIX "key."
#define CONFIG_GAMEPAD_PREFIX  "gamepad"
#define PREFIX_LEN(prefix)	   (sizeof(prefix) - 1)

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

int bindings_get(const Bindings *b, BindKind kind, int player, int index) {
	switch (kind) {
	case BIND_PAD:
		return b->pad[player][index];
	case BIND_GAMEPAD:
		return b->gamepad[player][index];
	default:
		return b->shortcut[index];
	}
}

void bindings_set(Bindings *b, BindKind kind, int player, int index, int code) {
	switch (kind) {
	case BIND_PAD:
		b->pad[player][index] = code;
		break;
	case BIND_GAMEPAD:
		b->gamepad[player][index] = code;
		break;
	default:
		b->shortcut[index] = code;
		break;
	}
}

/* A gamepad button may only be bound once per player; both players may use the same one. */
static bool gamepad_conflict(const Bindings *b, int player, int index, int code, char *msg,
							 size_t size) {
	for (int i = 0; i < JOY_BTN_COUNT; i++) {
		if (i != index && b->gamepad[player][i] == code) {
			snprintf(msg, size, "Already bound to %s", PAD_NAMES[i].label);
			return true;
		}
	}
	return false;
}

/* Keyboard keys are unique across both players and the shortcuts. */
bool bindings_conflict(const Bindings *b, BindKind kind, int player, int index, int scancode,
					   char *msg, size_t size) {
	if (kind == BIND_GAMEPAD) {
		return gamepad_conflict(b, player, index, scancode, msg, size);
	}
	if (platform_key_reserved(scancode)) {
		snprintf(msg, size, "Reserved by the emulator");
		return true;
	}

	ShortcutAction other = bindings_shortcut_for_key(b, scancode);

	if (other != SC_NONE && !(kind == BIND_SHORTCUT && (int)other == index)) {
		snprintf(msg, size, "%s%s", kind == BIND_PAD ? "Used by shortcut: " : "Already bound to ",
				 SHORTCUT_NAMES[other].label);
		return true;
	}

	for (int pl = 0; pl < MAX_PLAYERS; pl++) {
		for (int button = 0; button < JOY_BTN_COUNT; button++) {
			bool is_self = kind == BIND_PAD && pl == player && button == index;

			if (b->pad[pl][button] != scancode || is_self) {
				continue;
			}
			if (pl == player) {
				snprintf(msg, size, "Already bound to %s", PAD_NAMES[button].label);
			} else {
				snprintf(msg, size, "Used by Player %d: %s", pl + 1, PAD_NAMES[button].label);
			}
			return true;
		}
	}
	return false;
}

/* "joy" for player 1, "joy2" for player 2. */
static void config_prefix(char *out, size_t size, const char *base, int player) {
	if (player == 0) {
		snprintf(out, size, "%s.", base);
	} else {
		snprintf(out, size, "%s%d.", base, player + 1);
	}
}

void bindings_write(const Bindings *b, FILE *f) {
	char prefix[16];

	for (int player = 0; player < MAX_PLAYERS; player++) {
		config_prefix(prefix, sizeof prefix, CONFIG_PAD_PREFIX, player);
		for (int i = 0; i < JOY_BTN_COUNT; i++) {
			fprintf(f, "%s%s=%d\n", prefix, PAD_NAMES[i].config, b->pad[player][i]);
		}
	}
	for (int i = 0; i < SC_COUNT; i++) {
		fprintf(f, CONFIG_SHORTCUT_PREFIX "%s=%d\n", SHORTCUT_NAMES[i].config, b->shortcut[i]);
	}
	for (int player = 0; player < MAX_PLAYERS; player++) {
		config_prefix(prefix, sizeof prefix, CONFIG_GAMEPAD_PREFIX, player);
		for (int i = 0; i < JOY_BTN_COUNT; i++) {
			fprintf(f, "%s%s=%d\n", prefix, PAD_NAMES[i].config, b->gamepad[player][i]);
		}
	}
}

static void parse_pad(Bindings *b, int player, const char *config, const char *value) {
	int key;

	/* Older files stored "first,second"; the first key is the one that is kept. */
	if (sscanf(value, "%d", &key) != 1 || !platform_key_valid(key)) {
		return;
	}
	for (int i = 0; i < JOY_BTN_COUNT; i++) {
		if (strcmp(config, PAD_NAMES[i].config) == 0) {
			b->pad[player][i] = key;
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

static void parse_gamepad(Bindings *b, int player, const char *config, const char *value) {
	int code;

	if (sscanf(value, "%d", &code) != 1 || !platform_gamepad_code_valid(code)) {
		return;
	}
	for (int i = 0; i < JOY_BTN_COUNT; i++) {
		if (strcmp(config, PAD_NAMES[i].config) == 0) {
			b->gamepad[player][i] = code;
		}
	}
}

/* Matches "<base>." (player 1) or "<base>2." (player 2); returns the text after it. */
static const char *match_prefix(const char *name, const char *base, int *player) {
	size_t len = strlen(base);

	if (strncmp(name, base, len) != 0) {
		return NULL;
	}
	name += len;
	if (name[0] == '.') {
		*player = 0;
		return name + 1;
	}
	if (name[0] >= '2' && name[0] < '2' + MAX_PLAYERS - 1 && name[1] == '.') {
		*player = name[0] - '1';
		return name + 2;
	}
	return NULL;
}

void bindings_parse(Bindings *b, const char *name, const char *value) {
	int player;
	const char *rest;

	if ((rest = match_prefix(name, CONFIG_PAD_PREFIX, &player))) {
		parse_pad(b, player, rest, value);
	} else if (strncmp(name, CONFIG_SHORTCUT_PREFIX, PREFIX_LEN(CONFIG_SHORTCUT_PREFIX)) == 0) {
		parse_shortcut(b, name + PREFIX_LEN(CONFIG_SHORTCUT_PREFIX), value);
	} else if ((rest = match_prefix(name, CONFIG_GAMEPAD_PREFIX, &player))) {
		parse_gamepad(b, player, rest, value);
	}
}

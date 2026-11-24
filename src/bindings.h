#ifndef GBOYC_BINDINGS_H
#define GBOYC_BINDINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "joypad.h"

#define MAX_PLAYERS	 2 /* two linked consoles */
#define KEY_NONE	 0 /* unbound keyboard key */
#define GAMEPAD_NONE 0 /* unbound gamepad button; bound values are SDL button index + 1 */

typedef enum {
	SC_NONE = -1,
	SC_PAUSE,
	SC_SPEED,
	SC_RESET,
	SC_SAVE_STATE,
	SC_LOAD_STATE,
	SC_STEP_FRAME,
	SC_STEP_INSTR,
	SC_STEP_MCYCLE,
	SC_COUNT,
} ShortcutAction;

typedef enum {
	BIND_PAD,
	BIND_SHORTCUT,
	BIND_GAMEPAD,
} BindKind;

/*
 * Controls are per player (0 = Player 1, 1 = Player 2); shortcuts are shared.
 */
typedef struct {
	int pad[MAX_PLAYERS][JOY_BTN_COUNT];
	int shortcut[SC_COUNT];
	int gamepad[MAX_PLAYERS][JOY_BTN_COUNT];
} Bindings;

const char *bindings_pad_label(JoyButton button);
const char *bindings_shortcut_label(ShortcutAction action);
/* Returns SC_NONE if the key is not bound to any shortcut. */
ShortcutAction bindings_shortcut_for_key(const Bindings *b, int scancode);

/* player is ignored for shortcuts. */
int bindings_get(const Bindings *b, BindKind kind, int player, int index);
void bindings_set(Bindings *b, BindKind kind, int player, int index, int scancode);
bool bindings_conflict(const Bindings *b, BindKind kind, int player, int index, int scancode,
					   char *msg, size_t size);

void bindings_write(const Bindings *b, FILE *f);
/* Applies "joy[2].*" / "key.*" / "gamepad[2].*" config entries; unknown names are ignored. */
void bindings_parse(Bindings *b, const char *name, const char *value);

#endif

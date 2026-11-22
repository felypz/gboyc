#ifndef GBOYC_BINDINGS_H
#define GBOYC_BINDINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "joypad.h"

#define BIND_KEY_SLOTS 2
#define KEY_NONE	   0 /* unbound slot */

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
} BindKind;

typedef struct {
	int pad[JOY_BTN_COUNT][BIND_KEY_SLOTS];
	int shortcut[SC_COUNT];
} Bindings;

const char *bindings_pad_label(JoyButton button);
const char *bindings_shortcut_label(ShortcutAction action);
/* Returns SC_NONE if the key is not bound to any shortcut. */
ShortcutAction bindings_shortcut_for_key(const Bindings *b, int scancode);

int bindings_get(const Bindings *b, BindKind kind, int index, int slot);
void bindings_set(Bindings *b, BindKind kind, int index, int slot, int scancode);
bool bindings_conflict(const Bindings *b, BindKind kind, int index, int slot, int scancode,
					   char *msg, size_t size);

void bindings_write(const Bindings *b, FILE *f);
/* Applies "joy.*" / "key.*" config entries; unknown names are ignored. */
void bindings_parse(Bindings *b, const char *name, const char *value);

#endif

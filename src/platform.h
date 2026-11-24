#ifndef GBOYC_PLATFORM_H
#define GBOYC_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bindings.h"
#include "paths.h"
#include "ppu.h"

/* Logical size of the UI canvas: the Game Boy screen at 4x. */
#define UI_VIEW_W (PPU_W * 4)
#define UI_VIEW_H (PPU_H * 4)

/* Built-in bitmap font cell size (before the per-call scale). */
#define PLATFORM_GLYPH_W 8
#define PLATFORM_GLYPH_H 8

typedef struct Platform Platform;

typedef enum {
	PLATFORM_EV_QUIT,
	PLATFORM_EV_UP,
	PLATFORM_EV_DOWN,
	PLATFORM_EV_LEFT,
	PLATFORM_EV_RIGHT,
	PLATFORM_EV_CONFIRM,
	PLATFORM_EV_BACK,
	PLATFORM_EV_CLEAR,
	PLATFORM_EV_TEST, /* gamepad North: open the controller test where there is one */
	PLATFORM_EV_KEY,
	PLATFORM_EV_KEY_CAPTURED,
	PLATFORM_EV_GAMEPAD_BUTTON,	  /* key = button code; only sent while not capturing */
	PLATFORM_EV_GAMEPAD_CAPTURED, /* key = button code; sent while capturing */
	PLATFORM_EV_GAMEPAD_MENU,	  /* Back + Start pressed together on a gamepad */
	PLATFORM_EV_GAMEPAD_ADDED,	  /* path = gamepad name */
	PLATFORM_EV_GAMEPAD_REMOVED,  /* path = gamepad name */
	PLATFORM_EV_MOUSE_MOVE,
	PLATFORM_EV_MOUSE_CLICK,
	PLATFORM_EV_THEME_CHANGED,
	PLATFORM_EV_FILE_CHOSEN,
	PLATFORM_EV_DIALOG_CANCELED,
	PLATFORM_EV_DIALOG_FAILED,
} PlatformEventType;

typedef enum {
	PLATFORM_DIALOG_ROM,
	PLATFORM_DIALOG_BOOT_ROM,
} PlatformDialog;

/* What the next key / gamepad press means to the UI. */
typedef enum {
	PLATFORM_CAPTURE_OFF,
	/* Next press is reported as a *_CAPTURED event; only ESC (BACK) is special. */
	PLATFORM_CAPTURE_SINGLE,
	/* Like SINGLE, but ENTER (CONFIRM) and DEL (CLEAR) keep their meaning, for "bind all". */
	PLATFORM_CAPTURE_SEQUENCE,
	/* Keyboard as usual, but gamepad buttons are not reported (only the Back + Start chord). */
	PLATFORM_CAPTURE_MONITOR,
} PlatformCapture;

typedef struct {
	PlatformEventType type;
	int key;
	bool repeat;
	int mx;
	int my;
	char path[PATHS_MAX];
} PlatformEvent;

Platform *platform_create(const char *title, int scale, bool audio);
uint64_t platform_ticks_ms(void);
void platform_destroy(Platform *p);

bool platform_next_event(Platform *p, PlatformEvent *ev);
/*
 * Pressed buttons of one player as a JOY_MASK() bit mask. With one player every connected
 * gamepad counts; with two, gamepad N belongs to player N (in connection order).
 */
uint8_t platform_joypad(const Platform *p, const Bindings *bindings, int player, int players);

/* Defaults for one player's keyboard / gamepad binds (the menu button belongs to player 0). */
void platform_default_pad(Bindings *b, int player);
void platform_default_shortcuts(Bindings *b);
void platform_default_gamepad(Bindings *b, int player);
const char *platform_key_name(int scancode);
bool platform_key_valid(int scancode);
/* Scancode of the key that opens the controller test (sent as a plain PLATFORM_EV_KEY). */
int platform_test_key(void);
bool platform_key_reserved(int scancode);
int platform_slot_for_key(int scancode);
void platform_capture_input(Platform *p, PlatformCapture mode);

/* Gamepad buttons are stored as SDL button index + 1, so that GAMEPAD_NONE (0) means unbound. */
const char *platform_gamepad_button_name(int code);
bool platform_gamepad_code_valid(int code);
/* Menu navigation for a gamepad button; returns false if the button has none. */
bool platform_gamepad_nav(int code, PlatformEventType *nav);
int platform_gamepad_count(const Platform *p);
/* Raw state of the Nth connected gamepad, for the controller test screen. */
/* Bit numbers of PlatformPadState.buttons (the buttons every pad has). */
enum {
	PLATFORM_BTN_SOUTH,
	PLATFORM_BTN_EAST,
	PLATFORM_BTN_WEST,
	PLATFORM_BTN_NORTH,
	PLATFORM_BTN_BACK,
	PLATFORM_BTN_GUIDE,
	PLATFORM_BTN_START,
	PLATFORM_BTN_LEFT_STICK,
	PLATFORM_BTN_RIGHT_STICK,
	PLATFORM_BTN_LEFT_BUMPER,
	PLATFORM_BTN_RIGHT_BUMPER,
	PLATFORM_BTN_DPAD_UP,
	PLATFORM_BTN_DPAD_DOWN,
	PLATFORM_BTN_DPAD_LEFT,
	PLATFORM_BTN_DPAD_RIGHT,
};
/* Index into PlatformPadState.axes. */
enum {
	PLATFORM_AXIS_LEFT_X,
	PLATFORM_AXIS_LEFT_Y, /* positive = down */
	PLATFORM_AXIS_RIGHT_X,
	PLATFORM_AXIS_RIGHT_Y,
	PLATFORM_AXIS_LEFT_TRIGGER,
	PLATFORM_AXIS_RIGHT_TRIGGER,
};
#define PLATFORM_STICK_DEADZONE	 12000 /* of 32767: the left stick acts as a D-pad past this */
#define PLATFORM_TRIGGER_PRESSED 8000  /* a trigger counts as a button past this */
#define PLATFORM_PAD_AXES		 6	   /* left X/Y, right X/Y, left trigger, right trigger */
typedef struct {
	uint32_t buttons;				 /* bit N = SDL button N (gamepad code N + 1) */
	int16_t axes[PLATFORM_PAD_AXES]; /* -32768..32767; triggers 0..32767 */
} PlatformPadState;
bool platform_gamepad_state(const Platform *p, int index, PlatformPadState *out);
/* Name of the Nth connected gamepad (connection order), or NULL if there is none. */
const char *platform_gamepad_name(const Platform *p, int index);

bool platform_system_dark(void);
void platform_show_cursor(bool show);

void platform_set_background(Platform *p, uint32_t argb);

void platform_clear(Platform *p, uint32_t argb);
void platform_fill_rect(Platform *p, int x, int y, int w, int h, uint32_t argb);
void platform_draw_text(Platform *p, int x, int y, int scale, uint32_t argb, const char *text);
void platform_draw_frame(Platform *p, const uint32_t *framebuffer);
/* Two linked consoles side by side, each at half width. */
void platform_draw_frames(Platform *p, const uint32_t *left, const uint32_t *right);
void platform_flip(Platform *p);

void platform_open_file_dialog(Platform *p, PlatformDialog kind);
bool platform_make_dir(const char *path);

void platform_audio_push(Platform *p, const float *samples, size_t frames);
void platform_audio_clear(Platform *p);

/* Sleeps until the next frame is due (one Game Boy frame = GB_DOTS_PER_FRAME / GB_CPU_HZ). */
void platform_frame_limit(Platform *p);

#endif

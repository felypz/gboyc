#ifndef GBOYC_PLATFORM_H
#define GBOYC_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "joypad.h"

#define GB_SCREEN_W 160
#define GB_SCREEN_H 144

#define UI_VIEW_W (GB_SCREEN_W * 2)
#define UI_VIEW_H (GB_SCREEN_H * 2)

#define PLATFORM_PATH_MAX 4096

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
	PLATFORM_EV_PAUSE,
	PLATFORM_EV_SPEED,
	PLATFORM_EV_SAVE_STATE,
	PLATFORM_EV_LOAD_STATE,
	PLATFORM_EV_SLOT,
	PLATFORM_EV_STEP_FRAME,
	PLATFORM_EV_STEP_INSTR,
	PLATFORM_EV_STEP_CYCLE,
	PLATFORM_EV_KEY_CAPTURED,
	PLATFORM_EV_FILE_CHOSEN,
	PLATFORM_EV_DIALOG_CANCELED,
	PLATFORM_EV_DIALOG_FAILED,
} PlatformEventType;

typedef struct {
	PlatformEventType type;
	int slot;
	int key;
	const char *hotkey;
	char path[PLATFORM_PATH_MAX];
} PlatformEvent;

Platform *platform_create(const char *title, int scale);
void platform_destroy(Platform *p);

bool platform_next_event(Platform *p, PlatformEvent *ev);
uint8_t platform_joypad(const Platform *p, const JoyKeymap *keymap);

void platform_default_keymap(JoyKeymap *keymap);
const char *platform_key_name(int scancode);
bool platform_key_valid(int scancode);
void platform_capture_keys(Platform *p, bool on);

void platform_set_view(Platform *p, int w, int h);

void platform_clear(Platform *p, uint32_t argb);
void platform_fill_rect(Platform *p, int x, int y, int w, int h, uint32_t argb);
void platform_draw_text(Platform *p, int x, int y, int scale, uint32_t argb, const char *text);
void platform_draw_text_centered(Platform *p, int y, int scale, uint32_t argb, const char *text);
void platform_flip(Platform *p);

void platform_open_file_dialog(Platform *p, bool boot);
bool platform_make_dir(const char *path);

void platform_draw_frame(Platform *p, const uint32_t *framebuffer);

void platform_audio_push(Platform *p, const float *samples, size_t frames);
void platform_audio_clear(Platform *p);

void platform_frame_limit(Platform *p);

#endif

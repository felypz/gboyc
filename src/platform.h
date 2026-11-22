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
	PLATFORM_EV_KEY,
	PLATFORM_EV_KEY_CAPTURED,
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
/* Pressed buttons as a JOY_MASK() bit mask. */
uint8_t platform_joypad(const Bindings *bindings);

void platform_default_pad(Bindings *b);
void platform_default_shortcuts(Bindings *b);
const char *platform_key_name(int scancode);
bool platform_key_valid(int scancode);
bool platform_key_reserved(int scancode);
int platform_slot_for_key(int scancode);
void platform_capture_keys(Platform *p, bool on);

bool platform_system_dark(void);
void platform_show_cursor(bool show);

void platform_set_background(Platform *p, uint32_t argb);

void platform_clear(Platform *p, uint32_t argb);
void platform_fill_rect(Platform *p, int x, int y, int w, int h, uint32_t argb);
void platform_draw_text(Platform *p, int x, int y, int scale, uint32_t argb, const char *text);
void platform_draw_frame(Platform *p, const uint32_t *framebuffer);
void platform_flip(Platform *p);

void platform_open_file_dialog(Platform *p, PlatformDialog kind);
bool platform_make_dir(const char *path);

void platform_audio_push(Platform *p, const float *samples, size_t frames);
void platform_audio_clear(Platform *p);

/* Sleeps until the next frame is due (one Game Boy frame = GB_DOTS_PER_FRAME / GB_CPU_HZ). */
void platform_frame_limit(Platform *p);

#endif

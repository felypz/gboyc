#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot.h"
#include "cartridge.h"
#include "gb.h"
#include "palette.h"
#include "platform.h"
#include "state.h"
#include "ui.h"

#define AUTOSAVE_FRAMES 300
#define OSD_FRAMES		90
#define FAST_SPEED		2

typedef enum {
	APP_MENU,
	APP_RUNNING,
} AppState;

static AppState app_state = APP_MENU;
static Ui ui;
static Gb gb;
static Cartridge *cart;
static char rom_path[PLATFORM_PATH_MAX];
static int slot = 1;
static bool paused;
static int speed = 1;
static char osd[24];
static unsigned osd_frames;

static void osd_show(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(osd, sizeof osd, fmt, args);
	va_end(args);
	osd_frames = OSD_FRAMES;
}

static void draw_label(Platform *plat, int x, int y, const char *text) {
	int w = (int)strlen(text) * 8 + 4;

	platform_fill_rect(plat, x, y, w, 12, DMG_COLOR_0);
	platform_draw_text(plat, x + 2, y + 2, 1, DMG_COLOR_3, text);
}

static void draw_overlay(Platform *plat) {
	if (osd_frames) {
		draw_label(plat, 0, 0, osd);
		osd_frames--;
	}
	if (speed > 1) {
		char tag[16];

		snprintf(tag, sizeof tag, "%dx", speed);
		draw_label(plat, GB_SCREEN_W - ((int)strlen(tag) * 8 + 4), 0, tag);
	}
	if (paused) {
		draw_label(plat, 0, GB_SCREEN_H - 12, "PAUSED");
	}
}

static void unload_game(void) {
	cartridge_free(cart);
	cart = NULL;
	gb.cart = NULL;
}

static bool start_game(const char *path) {
	unload_game();

	cart = cartridge_load(path);
	if (!cart) {
		return false;
	}

	uint8_t boot[BOOT_ROM_SIZE];
	gb_init(&gb, cart, boot_load(boot) ? boot : NULL);
	snprintf(rom_path, sizeof rom_path, "%s", path);
	paused = false;
	speed = 1;
	slot = 1;
	osd_frames = 0;
	printf("ROM loaded: %s\n", cartridge_title(cart));
	return true;
}

static void back_to_menu(Platform *plat) {
	unload_game();
	platform_audio_clear(plat);
	app_state = APP_MENU;
	ui_init(&ui);
	platform_set_view(plat, UI_VIEW_W, UI_VIEW_H);
}

static void drain_audio(void) {
	const float *samples;

	gb_take_audio(&gb, &samples);
}

static void push_audio(Platform *plat) {
	const float *samples;
	size_t frames = gb_take_audio(&gb, &samples);

	if (speed == 1) {
		platform_audio_push(plat, samples, frames);
	}
}

static void save_state(void) {
	char path[STATE_PATH_MAX];

	if (!platform_make_dir(STATE_DIR)) {
		osd_show("Dir error");
		return;
	}

	state_path(path, sizeof path, rom_path, slot);
	osd_show(state_save(&gb, path) ? "Saved slot %d" : "Save failed %d", slot);
}

static void load_state(Platform *plat) {
	char path[STATE_PATH_MAX];

	state_path(path, sizeof path, rom_path, slot);

	switch (state_load(&gb, path)) {
	case STATE_OK:
		platform_audio_clear(plat);
		drain_audio();
		osd_show("Loaded slot %d", slot);
		break;
	case STATE_MISSING:
		osd_show("Empty slot %d", slot);
		break;
	default:
		osd_show("Bad state %d", slot);
		break;
	}
}

static bool handle_running_event(Platform *plat, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_QUIT:
		return false;
	case PLATFORM_EV_BACK:
		back_to_menu(plat);
		break;
	case PLATFORM_EV_PAUSE:
		paused = !paused;
		platform_audio_clear(plat);
		break;
	case PLATFORM_EV_SPEED:
		speed = (speed == 1) ? FAST_SPEED : 1;
		platform_audio_clear(plat);
		osd_show("Speed %dx", speed);
		break;
	case PLATFORM_EV_SLOT:
		slot = ev->slot;
		osd_show("Slot %d", slot);
		break;
	case PLATFORM_EV_SAVE_STATE:
		save_state();
		break;
	case PLATFORM_EV_LOAD_STATE:
		load_state(plat);
		break;
	default:
		break;
	}
	return true;
}

static void run_emulation(Platform *plat, unsigned *frames) {
	gb_set_joypad(&gb, platform_joypad(plat));
	for (int i = 0; i < speed; i++) {
		gb_run_frame(&gb);
	}
	push_audio(plat);

	if (++*frames % AUTOSAVE_FRAMES == 0) {
		cartridge_save(cart);
	}
}

int main(void) {
	Platform *plat = platform_create("gboyc", 4);
	if (!plat) {
		return EXIT_FAILURE;
	}

	ui_init(&ui);

	bool running = true;
	unsigned frames = 0;

	while (running) {
		PlatformEvent ev;

		while (platform_next_event(plat, &ev)) {
			if (app_state == APP_MENU) {
				switch (ui_handle_event(&ui, plat, &ev)) {
				case UI_ACTION_QUIT:
					running = false;
					break;
				case UI_ACTION_LOAD_ROM:
					if (start_game(ui.rom_path)) {
						app_state = APP_RUNNING;
						platform_audio_clear(plat);
						platform_set_view(plat, GB_SCREEN_W, GB_SCREEN_H);
					}
					break;
				case UI_ACTION_NONE:
					break;
				}
			} else if (!handle_running_event(plat, &ev)) {
				running = false;
			}
		}

		if (app_state == APP_MENU) {
			ui_draw(&ui, plat);
		} else {
			if (!paused) {
				run_emulation(plat, &frames);
			}
			platform_draw_frame(plat, gb_framebuffer(&gb));
			draw_overlay(plat);
			platform_flip(plat);
		}
		platform_frame_limit(plat);
	}

	unload_game();
	platform_destroy(plat);
	return EXIT_SUCCESS;
}

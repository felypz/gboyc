#include <stdio.h>
#include <stdlib.h>

#include "cartridge.h"
#include "gb.h"
#include "platform.h"
#include "ui.h"

#define AUTOSAVE_FRAMES 300

typedef enum {
	APP_MENU,
	APP_RUNNING,
} AppState;

static Ui ui;
static Gb gb;
static Cartridge *cart;

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

	gb_init(&gb, cart);
	printf("ROM loaded: %s\n", cartridge_title(cart));
	return true;
}

static void push_audio(Platform *plat) {
	const float *samples;
	size_t frames = gb_take_audio(&gb, &samples);

	platform_audio_push(plat, samples, frames);
}

int main(void) {
	Platform *plat = platform_create("gboyc", 4);
	if (!plat) {
		return EXIT_FAILURE;
	}

	ui_init(&ui);

	AppState state = APP_MENU;
	bool running = true;
	unsigned frames = 0;

	while (running) {
		PlatformEvent ev;

		while (platform_next_event(plat, &ev)) {
			if (state == APP_MENU) {
				switch (ui_handle_event(&ui, plat, &ev)) {
				case UI_ACTION_QUIT:
					running = false;
					break;
				case UI_ACTION_LOAD_ROM:
					if (start_game(ui.rom_path)) {
						state = APP_RUNNING;
						platform_audio_clear(plat);
						platform_set_view(plat, GB_SCREEN_W, GB_SCREEN_H);
					}
					break;
				case UI_ACTION_NONE:
					break;
				}
			} else if (ev.type == PLATFORM_EV_QUIT) {
				running = false;
			} else if (ev.type == PLATFORM_EV_BACK) {
				unload_game();
				platform_audio_clear(plat);
				state = APP_MENU;
				ui_init(&ui);
				platform_set_view(plat, UI_VIEW_W, UI_VIEW_H);
			}
		}

		if (state == APP_MENU) {
			ui_draw(&ui, plat);
		} else {
			gb_set_joypad(&gb, platform_joypad(plat));
			gb_run_frame(&gb);
			platform_present(plat, gb_framebuffer(&gb));
			push_audio(plat);
			if (++frames % AUTOSAVE_FRAMES == 0) {
				cartridge_save(cart);
			}
		}
		platform_frame_limit(plat);
	}

	unload_game();
	platform_destroy(plat);
	return EXIT_SUCCESS;
}

#include <stdio.h>
#include <stdlib.h>

#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "platform.h"
#include "ui.h"

#define AUTOSAVE_FRAMES 300

typedef enum {
	APP_MENU,
	APP_RUNNING,
} AppState;

static Ui ui;
static Cpu cpu;
static Cartridge *cart;

static void unload_game(void) {
	cartridge_free(cart);
	cart = NULL;
}

static bool start_game(const char *path) {
	unload_game();

	cart = cartridge_load(path);
	if (!cart) {
		return false;
	}

	bus_init(cart);
	cpu_reset(&cpu);
	printf("ROM loaded: %s\n", cartridge_title(cart));
	return true;
}

static void run_frame(void) {
	do {
		bus_tick(cpu_step(&cpu));
	} while (!bus_frame_ready());
}

static void push_audio(Platform *plat) {
	const float *samples;
	size_t frames = bus_take_audio(&samples);

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
			bus_set_joypad(platform_joypad(plat));
			run_frame();
			platform_present(plat, bus_framebuffer());
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

#include <stdio.h>
#include <stdlib.h>

#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "palette.h"
#include "platform.h"
#include "ui.h"

#define CYCLES_PER_FRAME 70224

typedef enum {
	APP_MENU,
	APP_RUNNING,
} AppState;

static uint32_t framebuffer[GB_SCREEN_W * GB_SCREEN_H];
static Ui ui;
static Cpu cpu;
static Cartridge *cart;

static void draw_test_pattern(void) {
	static const uint32_t palette[4] = {DMG_COLOR_0, DMG_COLOR_1, DMG_COLOR_2, DMG_COLOR_3};

	for (int y = 0; y < GB_SCREEN_H; y++) {
		for (int x = 0; x < GB_SCREEN_W; x++) {
			framebuffer[y * GB_SCREEN_W + x] = palette[x / (GB_SCREEN_W / 4)];
		}
	}
}

static bool start_game(const char *path) {
	Cartridge *loaded = cartridge_load(path);
	if (!loaded) {
		return false;
	}

	cartridge_free(cart);
	cart = loaded;
	bus_init(cart);
	cpu_reset(&cpu);
	printf("ROM loaded: %s\n", cartridge_title(cart));
	return true;
}

static void run_frame(void) {
	for (int cycles = 0; cycles < CYCLES_PER_FRAME;) {
		int n = cpu_step(&cpu);
		bus_tick(n);
		cycles += n;
	}
}

int main(void) {
	Platform *plat = platform_create("gboyc", 4);
	if (!plat) {
		return EXIT_FAILURE;
	}

	draw_test_pattern();
	ui_init(&ui);

	AppState state = APP_MENU;
	bool running = true;

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
						platform_set_view(plat, GB_SCREEN_W, GB_SCREEN_H);
					}
					break;
				case UI_ACTION_NONE:
					break;
				}
			} else if (ev.type == PLATFORM_EV_QUIT) {
				running = false;
			} else if (ev.type == PLATFORM_EV_BACK) {
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
			platform_present(plat, framebuffer);
		}
		platform_frame_limit(plat);
	}

	cartridge_free(cart);
	platform_destroy(plat);
	return EXIT_SUCCESS;
}

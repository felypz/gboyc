#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bindings.h"
#include "boot.h"
#include "cartridge.h"
#include "gb.h"
#include "platform.h"
#include "settings.h"
#include "state.h"
#include "stepper.h"
#include "ui.h"

#define AUTOSAVE_FRAMES 300
#define OSD_FRAMES		90
#define FAST_SPEED		2

typedef enum {
	APP_MENU,
	APP_RUNNING,
} AppState;

static AppState app_state = APP_MENU;
static bool menu_open;
static bool cursor_visible = true;
static Settings settings;
static Ui ui;
static Gb gb;
static Stepper stepper;
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

static void draw_hud(Platform *plat) {
	Hud hud = {
		.toast = osd_frames ? osd : NULL,
		.speed = speed,
		.paused = paused,
		.pc = gb.cpu.pc,
		.mid_instruction = stepper.pending,
		.cycles = stepper.cycles,
	};

	if (osd_frames) {
		osd_frames--;
	}
	ui_draw_hud(&ui, plat, &hud);
}

static void unload_game(void) {
	stepper_discard(&stepper);
	cartridge_free(cart);
	cart = NULL;
	gb.cart = NULL;
}

static void init_gb(void) {
	uint8_t boot[BOOT_ROM_SIZE];
	bool use_boot = settings.boot_enabled && boot_load(settings.boot_file, boot);

	gb_init(&gb, cart, use_boot ? boot : NULL);
}

static bool start_game(const char *path) {
	cartridge_save(cart);

	Cartridge *loaded = cartridge_load(path);

	if (!loaded) {
		return false;
	}

	unload_game();
	cart = loaded;
	init_gb();
	snprintf(rom_path, sizeof rom_path, "%s", path);
	paused = false;
	speed = 1;
	slot = 1;
	osd_frames = 0;
	printf("ROM loaded: %s\n", cartridge_title(cart));
	return true;
}

static void reset_game(Platform *plat) {
	stepper_discard(&stepper);
	cartridge_save(cart);
	cartridge_reset(cart);
	init_gb();
	paused = false;
	platform_audio_clear(plat);
	osd_show("Reset");
}

static void enter_game(Platform *plat) {
	app_state = APP_RUNNING;
	menu_open = false;
	platform_audio_clear(plat);
}

static void open_menu(Platform *plat) {
	menu_open = true;
	ui_open_pause(&ui);
	platform_audio_clear(plat);
}

static void close_menu(Platform *plat) {
	menu_open = false;
	platform_audio_clear(plat);
}

static void back_to_main(Platform *plat) {
	unload_game();
	menu_open = false;
	platform_audio_clear(plat);
	app_state = APP_MENU;
	ui_init(&ui, &settings);
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

	stepper_commit(&stepper, &gb);
	if (!state_ensure_dir()) {
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
		stepper_discard(&stepper);
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

static void toggle_pause(Platform *plat) {
	stepper_commit(&stepper, &gb);
	paused = !paused;
	platform_audio_clear(plat);
	drain_audio();
}

static void step(Platform *plat, ShortcutAction action) {
	if (!paused) {
		osd_show("Pause to step");
		return;
	}
	if (!stepper.pending) {
		gb_set_joypad(&gb, platform_joypad(plat, &settings.bindings));
	}

	switch (action) {
	case SC_STEP_FRAME:
		stepper_frame(&stepper, &gb);
		break;
	case SC_STEP_INSTR:
		stepper_instruction(&stepper, &gb);
		break;
	default:
		stepper_cycle(&stepper, &gb);
		break;
	}
	drain_audio();
}

static void handle_shortcut(Platform *plat, ShortcutAction action) {
	switch (action) {
	case SC_PAUSE:
		toggle_pause(plat);
		break;
	case SC_SPEED:
		speed = (speed == 1) ? FAST_SPEED : 1;
		platform_audio_clear(plat);
		osd_show("Speed %dx", speed);
		break;
	case SC_RESET:
		reset_game(plat);
		break;
	case SC_SAVE_STATE:
		save_state();
		break;
	case SC_LOAD_STATE:
		load_state(plat);
		break;
	case SC_STEP_FRAME:
	case SC_STEP_INSTR:
	case SC_STEP_CYCLE:
		step(plat, action);
		break;
	default:
		break;
	}
}

static bool handle_running_event(Platform *plat, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_QUIT:
		return false;
	case PLATFORM_EV_THEME_CHANGED:
		ui.system_dark = platform_system_dark();
		return true;
	case PLATFORM_EV_BACK:
		if (!ev->repeat) {
			open_menu(plat);
		}
		return true;
	default:
		break;
	}

	if (ev->key <= 0) {
		return true;
	}

	int slot_key = platform_slot_for_key(ev->key);

	if (slot_key) {
		if (!ev->repeat) {
			slot = slot_key;
			osd_show("Slot %d", slot);
		}
		return true;
	}

	int action = bindings_shortcut_for_key(&settings.bindings, ev->key);

	if (action >= 0 && (!ev->repeat || action == SC_STEP_FRAME)) {
		handle_shortcut(plat, (ShortcutAction)action);
	}
	return true;
}

static bool handle_ui_event(Platform *plat, const PlatformEvent *ev) {
	switch (ui_handle_event(&ui, plat, ev)) {
	case UI_ACTION_QUIT:
		return false;
	case UI_ACTION_LOAD_ROM:
		if (start_game(ui.rom_path)) {
			enter_game(plat);
		} else {
			ui_notify(&ui, "Invalid or unsupported ROM", true);
		}
		break;
	case UI_ACTION_RESUME:
		close_menu(plat);
		break;
	case UI_ACTION_RESET:
		reset_game(plat);
		close_menu(plat);
		break;
	case UI_ACTION_MAIN_MENU:
		back_to_main(plat);
		break;
	case UI_ACTION_NONE:
		break;
	}
	return true;
}

static void run_emulation(Platform *plat, unsigned *frames) {
	gb_set_joypad(&gb, platform_joypad(plat, &settings.bindings));
	for (int i = 0; i < speed; i++) {
		gb_run_frame(&gb);
	}
	push_audio(plat);

	if (++*frames % AUTOSAVE_FRAMES == 0) {
		cartridge_save(cart);
	}
}

static void update_cursor(Platform *plat) {
	bool want = app_state == APP_MENU || menu_open;

	if (want != cursor_visible) {
		platform_show_cursor(plat, want);
		cursor_visible = want;
	}
}

int main(void) {
	settings_load(&settings);

	Platform *plat = platform_create("gboyc", 4);
	if (!plat) {
		return EXIT_FAILURE;
	}

	ui_init(&ui, &settings);

	bool running = true;
	unsigned frames = 0;

	while (running) {
		PlatformEvent ev;

		while (platform_next_event(plat, &ev)) {
			bool keep = (app_state == APP_MENU || menu_open) ? handle_ui_event(plat, &ev)
															 : handle_running_event(plat, &ev);

			if (!keep) {
				running = false;
			}
		}

		update_cursor(plat);

		if (app_state == APP_MENU) {
			ui_draw(&ui, plat);
		} else {
			if (!paused && !menu_open) {
				run_emulation(plat, &frames);
			}
			platform_set_background(plat, 0xFF000000);
			platform_draw_frame(plat, gb_framebuffer(&gb));
			if (menu_open) {
				ui_draw(&ui, plat);
			} else {
				draw_hud(plat);
			}
		}

		platform_flip(plat);
		platform_frame_limit(plat);
	}

	unload_game();
	stepper_free(&stepper);
	platform_destroy(plat);
	return EXIT_SUCCESS;
}

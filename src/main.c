#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bindings.h"
#include "boot.h"
#include "cartridge.h"
#include "cli.h"
#include "gb.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "state.h"
#include "stepper.h"
#include "ui.h"
#include "verbose.h"

#define AUTOSAVE_FRAMES	  300
#define OSD_FRAMES		  90
#define FAST_SPEED		  2
#define STATS_INTERVAL_MS 1000

typedef enum {
	APP_MENU,
	APP_RUNNING,
} AppState;

static AppState app_state = APP_MENU;
static bool menu_open;
static bool cursor_visible = true;
static CliOptions cli;
static uint8_t cli_boot[BOOT_ROM_SIZE];
static bool cli_boot_loaded;
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
static uint64_t stats_start;
static unsigned stats_frames;

static void osd_show(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(osd, sizeof osd, fmt, args);
	va_end(args);
	osd_frames = OSD_FRAMES;
	verbose("%s", osd);
}

static void stats_reset(void) {
	if (verbose_enabled()) {
		stats_start = platform_ticks_ms();
		stats_frames = 0;
	}
}

static void stats_tick(void) {
	if (!verbose_enabled()) {
		return;
	}

	stats_frames += (unsigned)speed;

	uint64_t now = platform_ticks_ms();
	uint64_t elapsed = now - stats_start;

	if (elapsed < STATS_INTERVAL_MS) {
		return;
	}

	verbose("%.1f emulated fps (speed %dx)", (double)stats_frames * 1000.0 / (double)elapsed,
			speed);
	stats_start = now;
	stats_frames = 0;
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
	const uint8_t *rom = NULL;

	if (cli_boot_loaded) {
		rom = cli_boot;
		verbose("Boot: using %s (--boot-rom)", cli.boot_rom);
	} else if (cli.no_boot) {
		verbose("Boot: skipped (--no-boot)");
	} else if (!settings.boot_enabled) {
		verbose("Boot: disabled in settings");
	} else if (boot_load(settings.boot_file, boot)) {
		rom = boot;
		verbose("Boot: using %s", settings.boot_file);
	} else {
		verbose("Boot: no boot ROM available, starting in the post-boot state");
	}
	gb_init(&gb, cart, rom);
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
	verbose("ROM file: %s", path);
	cartridge_log_info(cart);
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
	stats_reset();
}

static void open_menu(Platform *plat) {
	menu_open = true;
	ui_open_pause(&ui);
	platform_audio_clear(plat);
	verbose("Pause menu opened");
}

static void close_menu(Platform *plat) {
	menu_open = false;
	platform_audio_clear(plat);
	verbose("Pause menu closed");
}

static void back_to_main(Platform *plat) {
	unload_game();
	menu_open = false;
	platform_audio_clear(plat);
	app_state = APP_MENU;
	ui_init(&ui, &settings);
	verbose("Back to the main menu");
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
	verbose("State file: %s", path);
}

static bool load_state(Platform *plat) {
	char path[STATE_PATH_MAX];

	state_path(path, sizeof path, rom_path, slot);

	switch (state_load(&gb, path)) {
	case STATE_OK:
		stepper_discard(&stepper);
		platform_audio_clear(plat);
		drain_audio();
		osd_show("Loaded slot %d", slot);
		verbose("State file: %s", path);
		return true;
	case STATE_MISSING:
		osd_show("Empty slot %d", slot);
		verbose("No state file: %s", path);
		return false;
	default:
		osd_show("Bad state %d", slot);
		verbose("Invalid state file: %s", path);
		return false;
	}
}

static void toggle_pause(Platform *plat) {
	stepper_commit(&stepper, &gb);
	paused = !paused;
	platform_audio_clear(plat);
	drain_audio();
	verbose(paused ? "Paused" : "Resumed");
}

static void step(Platform *plat, ShortcutAction action) {
	if (!paused) {
		osd_show("Pause to step");
		return;
	}
	if (!stepper.pending) {
		gb_set_joypad(&gb, platform_joypad(plat, &settings.bindings));
	}

	const char *name;

	switch (action) {
	case SC_STEP_FRAME:
		stepper_frame(&stepper, &gb);
		name = "frame";
		break;
	case SC_STEP_INSTR:
		stepper_instruction(&stepper, &gb);
		name = "instruction";
		break;
	default:
		stepper_cycle(&stepper, &gb);
		name = "cycle";
		break;
	}
	drain_audio();
	verbose("Step %s: PC %04X", name, (unsigned)gb.cpu.pc);
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
	stats_tick();
}

static void update_cursor(Platform *plat) {
	bool want = app_state == APP_MENU || menu_open;

	if (want != cursor_visible) {
		platform_show_cursor(plat, want);
		cursor_visible = want;
	}
}

static void log_startup(void) {
	char dir[PATHS_MAX];

	verbose("gboyc %s", GBOYC_VERSION);
	if (paths_dir(PATHS_CONFIG, dir, sizeof dir)) {
		verbose("Config directory: %s", dir);
	}
	if (paths_dir(PATHS_DATA, dir, sizeof dir)) {
		verbose("Data directory: %s", dir);
	}
	verbose("Boot animation in settings: %s", settings.boot_enabled ? "on" : "off");
}

static bool load_cli_boot(void) {
	if (!cli.boot_rom) {
		return true;
	}

	BootResult result = boot_load_file(cli.boot_rom, cli_boot);

	if (result != BOOT_OK) {
		fprintf(stderr, "gboyc: boot ROM '%s': %s\n", cli.boot_rom, boot_result_text(result));
		return false;
	}
	cli_boot_loaded = true;
	return true;
}

static void start_from_cli(Platform *plat) {
	enter_game(plat);
	paused = cli.paused;
	if (paused) {
		verbose("Starting paused (--paused)");
	}
	if (cli.state) {
		slot = cli.state;
		if (!load_state(plat)) {
			fprintf(stderr, "gboyc: warning: slot %d has no valid state\n", slot);
		}
	}
}

int main(int argc, char **argv) {
	switch (cli_parse(&cli, argc, argv)) {
	case CLI_EXIT_OK:
		return EXIT_SUCCESS;
	case CLI_EXIT_FAIL:
		return EXIT_FAILURE;
	case CLI_RUN:
		break;
	}

	verbose_enable(cli.verbose);
	settings_load(&settings);
	log_startup();

	if (!load_cli_boot()) {
		return EXIT_FAILURE;
	}
	if (cli.rom && !start_game(cli.rom)) {
		fprintf(stderr, "gboyc: cannot load ROM '%s'\n", cli.rom);
		return EXIT_FAILURE;
	}

	Platform *plat = platform_create("gboyc", cli.scale, !cli.mute);

	if (!plat) {
		unload_game();
		return EXIT_FAILURE;
	}
	verbose("Window scale: %dx, audio %s", cli.scale, cli.mute ? "off" : "on");

	ui_init(&ui, &settings);
	if (cart) {
		start_from_cli(plat);
	}

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
			} else {
				stats_reset();
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

	verbose("Exiting");
	unload_game();
	stepper_free(&stepper);
	platform_destroy(plat);
	return EXIT_SUCCESS;
}

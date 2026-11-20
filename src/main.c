#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bindings.h"
#include "boot.h"
#include "cartridge.h"
#include "cli.h"
#include "crash.h"
#include "gb.h"
#include "guard.h"
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
#define CRASH_TEST_FRAMES 60

typedef enum {
	APP_MENU,
	APP_RUNNING,
} AppState;

typedef struct {
	ShortcutAction action;
} StepJob;

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
static bool crashed;
static bool crash_ignored;
static CrashReport crash_report;
static unsigned run_frames;
static bool crash_pending;

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
	crashed = false;
	crash_ignored = false;
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
	crashed = false;
	crash_ignored = false;
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
	crashed = false;
	crash_ignored = false;
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

static void enter_crash(Platform *plat) {
	crashed = true;
	crash_ignored = false;
	menu_open = true;
	ui_open_crash(&ui, &crash_report);
	platform_audio_clear(plat);
	crash_print(&crash_report, &gb, verbose_enabled());
}

static void fault(Platform *plat) {
	char reason[CRASH_LINE_LEN];
	char detail[CRASH_LINE_LEN];

	gb.step.abort = NULL;
	stepper_discard(&stepper);
	snprintf(reason, sizeof reason, "Emulator fault: %s", guard_signal_name(guard_signal()));
	snprintf(detail, sizeof detail, "Fault address %#lx",
			 (unsigned long)(uintptr_t)guard_address());
	crash_emulator(&crash_report, &gb, reason, detail);
	cartridge_discard_changes(cart);
	enter_crash(plat);
}

static void stall(Platform *plat) {
	crash_emulator(&crash_report, &gb, "Emulation stalled", "A frame did not finish in time");
	cartridge_discard_changes(cart);
	enter_crash(plat);
}

static void check_guest(Platform *plat) {
	if (!crash_ignored && crash_detect_game(&gb, &crash_report)) {
		enter_crash(plat);
	}
}

static void commit_job(void *arg) {
	(void)arg;
	stepper_commit(&stepper, &gb);
}

static bool commit_pending(Platform *plat) {
	if (!stepper.pending || guard_run(commit_job, NULL)) {
		return true;
	}
	fault(plat);
	return false;
}

static void save_state(Platform *plat) {
	char path[STATE_PATH_MAX];

	if (!commit_pending(plat)) {
		return;
	}
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
		crashed = false;
		crash_ignored = false;
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
	if (!commit_pending(plat)) {
		return;
	}
	paused = !paused;
	platform_audio_clear(plat);
	drain_audio();
	verbose(paused ? "Paused" : "Resumed");
}

static void step_job(void *arg) {
	const StepJob *job = arg;

	switch (job->action) {
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
}

static void step(Platform *plat, ShortcutAction action) {
	if (!paused) {
		osd_show("Pause to step");
		return;
	}
	if (!stepper.pending) {
		gb_set_joypad(&gb, platform_joypad(plat, &settings.bindings));
	}

	StepJob job = {action};

	if (!guard_run(step_job, &job)) {
		fault(plat);
		return;
	}
	drain_audio();
	verbose("Step: PC %04X", (unsigned)gb.cpu.pc);
	check_guest(plat);
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
		save_state(plat);
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

static void crash_reset(Platform *plat) {
	if (crashed && crash_report.kind == CRASH_EMULATOR) {
		char path[PLATFORM_PATH_MAX];

		snprintf(path, sizeof path, "%s", rom_path);
		if (!start_game(path)) {
			ui_notify(&ui, "Could not reload the ROM", true);
			return;
		}
		platform_audio_clear(plat);
	} else {
		reset_game(plat);
	}
	close_menu(plat);
}

static void resume_or_dismiss(Platform *plat) {
	if (!crashed) {
		close_menu(plat);
	} else if (crash_report.kind == CRASH_GAME) {
		crash_ignored = true;
		close_menu(plat);
	} else {
		ui_notify(&ui, "Unavailable after a fault", true);
	}
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
	case UI_ACTION_LOAD_STATE:
		if (load_state(plat)) {
			close_menu(plat);
		} else {
			ui_notify(&ui, "No valid state in this slot", true);
		}
		break;
	case UI_ACTION_RESUME:
		resume_or_dismiss(plat);
		break;
	case UI_ACTION_RESET:
		crash_reset(plat);
		break;
	case UI_ACTION_MAIN_MENU:
		back_to_main(plat);
		break;
	case UI_ACTION_NONE:
		break;
	}
	return true;
}

static void inject_crash(void) {
	const char *kind = cli.crash_test;

	verbose("Injecting test crash: %s", kind);
	gb.cpu.ime = false;
	gb.cpu.halted = false;
	gb.cpu.ei_delay = 0;

	if (strcmp(kind, "segv") == 0) {
		int *volatile bad = NULL;

		*bad = 1;
	} else if (strcmp(kind, "abort") == 0) {
		abort();
	} else if (strcmp(kind, "fpe") == 0) {
		volatile int zero = 0;
		volatile int result = 1 / zero;

		(void)result;
	} else if (strcmp(kind, "illegal") == 0) {
		gb.wram[0] = 0xDD;
		gb.cpu.pc = 0xC000;
	} else if (strcmp(kind, "halt") == 0) {
		gb.wram[0] = 0x76;
		gb.cpu.pc = 0xC000;
		gb.ie = 0;
	}
}

static void frame_job(void *arg) {
	bool *complete = arg;

	if (crash_pending) {
		crash_pending = false;
		inject_crash();
	}
	for (int i = 0; i < speed; i++) {
		if (!gb_run_frame(&gb)) {
			*complete = false;
			return;
		}
	}
}

static void run_emulation(Platform *plat, unsigned *frames) {
	bool complete = true;

	if (cli.crash_test && run_frames < CRASH_TEST_FRAMES && ++run_frames == CRASH_TEST_FRAMES) {
		crash_pending = true;
	}

	gb_set_joypad(&gb, platform_joypad(plat, &settings.bindings));
	if (!guard_run(frame_job, &complete)) {
		fault(plat);
		return;
	}
	if (!complete) {
		stall(plat);
		return;
	}

	push_audio(plat);
	if (++*frames % AUTOSAVE_FRAMES == 0) {
		cartridge_save(cart);
	}
	stats_tick();
	check_guest(plat);
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
	guard_install();
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

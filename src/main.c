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
#include "log.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "state.h"
#include "stepper.h"
#include "ui.h"

#define AUTOSAVE_FRAMES	  300 /* battery RAM is flushed every 5 s at 60 fps */
#define OSD_FRAMES		  90
#define FAST_SPEED		  2
#define STATS_INTERVAL_MS 1000
#define CRASH_TEST_FRAME  60 /* frame (after loading the ROM) at which --crash-test fires */
#define COLOR_BLACK		  0xFF000000U

typedef enum {
	APP_MENU,	 /* main menu, no game loaded */
	APP_RUNNING, /* game loaded; the pause/crash menu may be open on top */
} AppState;

/* Boot ROM given with --boot-rom; takes priority over the settings. */
typedef struct {
	uint8_t rom[BOOT_ROM_SIZE];
	bool loaded;
} BootOverride;

typedef struct {
	bool active;  /* a crash screen is (or was) shown for the current game */
	bool ignored; /* the user chose to keep going after a game crash */
	bool inject_pending;
	bool test_fired; /* --crash-test fires once per run */
	CrashReport report;
} CrashState;

typedef struct {
	Platform *plat;
	AppState state;
	bool menu_open; /* pause or crash menu drawn over the running game */
	bool cursor_visible;

	CliOptions cli;
	BootOverride boot_override;
	Settings settings;
	Ui ui;

	Gb gb;
	Stepper stepper;
	Cartridge *cart;
	char rom_path[PATHS_MAX];

	int slot; /* current savestate slot, 1-9 */
	bool paused;
	int speed;
	unsigned frames; /* frames run since the ROM was loaded */

	char osd[24];
	unsigned osd_frames;
	uint64_t stats_start;
	unsigned stats_frames;

	CrashState crash;
} App;

typedef struct {
	App *app;
	ShortcutAction action;
	bool stalled; /* the frame step ran out of budget without producing a frame */
} StepJob;

typedef struct {
	App *app;
	bool complete;
} FrameJob;

static App app = {.state = APP_MENU, .cursor_visible = true, .slot = 1, .speed = 1};

/* -- OSD and stats -- */

static void osd_show(App *a, const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vsnprintf(a->osd, sizeof a->osd, fmt, args);
	va_end(args);
	a->osd_frames = OSD_FRAMES;
	log_verbose("%s", a->osd);
}

static void stats_reset(App *a) {
	if (log_verbose_enabled()) {
		a->stats_start = platform_ticks_ms();
		a->stats_frames = 0;
	}
}

static void stats_tick(App *a) {
	if (!log_verbose_enabled()) {
		return;
	}

	a->stats_frames += (unsigned)a->speed;

	uint64_t now = platform_ticks_ms();
	uint64_t elapsed = now - a->stats_start;

	if (elapsed < STATS_INTERVAL_MS) {
		return;
	}

	log_verbose("%.1f emulated fps (speed %dx)", (double)a->stats_frames * 1000.0 / (double)elapsed,
				a->speed);
	a->stats_start = now;
	a->stats_frames = 0;
}

static void draw_hud(App *a) {
	Hud hud = {
		.toast = a->osd_frames ? a->osd : NULL,
		.speed = a->speed,
		.paused = a->paused,
		.pc = a->gb.cpu.pc,
		.mid_instruction = a->stepper.pending,
		.mcycles = a->stepper.mcycles,
	};

	if (a->osd_frames) {
		a->osd_frames--;
	}
	ui_draw_hud(&a->ui, a->plat, &hud);
}

/* -- Game lifecycle -- */

/* True while a menu (main, pause or crash) has the input focus instead of the game. */
static bool menu_has_focus(const App *a) {
	return a->state == APP_MENU || a->menu_open;
}

static void clear_crash(App *a) {
	a->crash.active = false;
	a->crash.ignored = false;
}

static void unload_game(App *a) {
	stepper_discard(&a->stepper);
	cartridge_free(a->cart);
	a->cart = NULL;
	a->gb.cart = NULL;
}

static void init_gb(App *a) {
	uint8_t installed[BOOT_ROM_SIZE];
	const uint8_t *boot_rom = NULL;
	const Settings *s = &a->settings;

	if (a->boot_override.loaded) {
		boot_rom = a->boot_override.rom;
		log_verbose("Boot: using %s (--boot-rom)", a->cli.boot_rom);
	} else if (a->cli.no_boot) {
		log_verbose("Boot: skipped (--no-boot)");
	} else if (!s->boot_rom_enabled) {
		log_verbose("Boot: disabled in settings");
	} else if (boot_load_installed(s->boot_rom_file, installed)) {
		boot_rom = installed;
		log_verbose("Boot: using %s", s->boot_rom_file);
	} else {
		log_verbose("Boot: no boot ROM available, starting in the post-boot state");
	}
	gb_init(&a->gb, a->cart, boot_rom);
}

static bool start_game(App *a, const char *path) {
	/* Flush the current game first: the new ROM may be the same file (and so the same .sav). */
	cartridge_flush_battery(a->cart);

	Cartridge *loaded = cartridge_load(path);

	if (!loaded) {
		return false;
	}

	unload_game(a);
	a->cart = loaded;
	init_gb(a);
	snprintf(a->rom_path, sizeof a->rom_path, "%s", path);
	a->paused = false;
	a->speed = 1;
	a->slot = 1;
	a->frames = 0;
	a->osd_frames = 0;
	clear_crash(a);
	log_info("ROM loaded: %s", cartridge_title(a->cart));
	log_verbose("ROM file: %s", path);
	cartridge_log_info(a->cart);
	return true;
}

static void reset_game(App *a) {
	stepper_discard(&a->stepper);
	cartridge_flush_battery(a->cart);
	cartridge_reset(a->cart);
	init_gb(a);
	a->paused = false;
	clear_crash(a);
	platform_audio_clear(a->plat);
	osd_show(a, "Reset");
}

static void enter_game(App *a) {
	a->state = APP_RUNNING;
	a->menu_open = false;
	platform_audio_clear(a->plat);
	stats_reset(a);
}

static void open_menu(App *a) {
	a->menu_open = true;
	ui_open_pause(&a->ui);
	platform_audio_clear(a->plat);
	log_verbose("Pause menu opened");
}

static void close_menu(App *a) {
	a->menu_open = false;
	platform_audio_clear(a->plat);
	log_verbose("Pause menu closed");
}

static void back_to_main(App *a) {
	unload_game(a);
	a->menu_open = false;
	clear_crash(a);
	platform_audio_clear(a->plat);
	a->state = APP_MENU;
	ui_init(&a->ui, &a->settings);
	log_verbose("Back to the main menu");
}

/* -- Audio -- */

static void drain_audio(App *a) {
	const float *samples;

	gb_take_audio(&a->gb, &samples);
}

static void push_audio(App *a) {
	const float *samples;
	size_t frames = gb_take_audio(&a->gb, &samples);

	if (a->speed == 1) {
		platform_audio_push(a->plat, samples, frames);
	}
}

/* -- Crashes -- */

/* Shows the crash screen for a.crash.report, which the caller has already filled in. */
static void enter_crash(App *a) {
	a->crash.active = true;
	a->crash.ignored = false;
	a->menu_open = true;
	if (a->crash.report.kind == CRASH_EMULATOR) {
		/* The machine state can't be trusted, so don't let it overwrite the real .sav. */
		cartridge_discard_battery_changes(a->cart);
	}
	ui_open_crash(&a->ui, &a->crash.report);
	platform_audio_clear(a->plat);
	crash_print(&a->crash.report, &a->gb, log_verbose_enabled());
}

/* The emulator itself crashed (signal caught by guard_run). */
static void enter_emulator_fault(App *a) {
	char reason[CRASH_LINE_LEN];
	char detail[CRASH_LINE_LEN];

	a->gb.step.escape = NULL; /* may point at a dead stack frame of the interrupted stepper */
	stepper_discard(&a->stepper);
	snprintf(reason, sizeof reason, "Emulator fault: %s", guard_signal_name(guard_caught_signal()));
	snprintf(detail, sizeof detail, "Fault address %#lx",
			 (unsigned long)(uintptr_t)guard_fault_address());
	crash_emulator(&a->crash.report, &a->gb, reason, detail);
	enter_crash(a);
}

/* gb_run_frame() gave up: the guest never produced a frame. */
static void enter_emulator_stall(App *a) {
	crash_emulator(&a->crash.report, &a->gb, "Emulation stalled", "A frame did not finish in time");
	enter_crash(a);
}

/* Detects a crash of the emulated game (illegal opcode, HALT that can never wake up). */
static void check_guest_crash(App *a) {
	if (!a->crash.ignored && crash_detect_game(&a->gb, &a->crash.report)) {
		enter_crash(a);
	}
}

/* -- Stepping and savestates -- */

static void commit_job(void *arg) {
	App *a = arg;

	stepper_commit(&a->stepper, &a->gb);
}

/* Finishes a half-executed instruction so the machine is in a consistent state. */
static bool commit_pending(App *a) {
	if (!a->stepper.pending || guard_run(commit_job, a)) {
		return true;
	}
	enter_emulator_fault(a);
	return false;
}

static void save_state(App *a) {
	char path[PATHS_MAX];

	if (!commit_pending(a)) {
		return;
	}
	if (!state_ensure_dir() || !state_path(path, sizeof path, a->rom_path, a->slot)) {
		osd_show(a, "Dir error");
		return;
	}

	osd_show(a, state_save(&a->gb, path) ? "Saved slot %d" : "Save failed %d", a->slot);
	log_verbose("State file: %s", path);
}

static bool load_state(App *a) {
	char path[PATHS_MAX];

	if (!state_path(path, sizeof path, a->rom_path, a->slot)) {
		osd_show(a, "Path error");
		return false;
	}

	switch (state_load(&a->gb, path)) {
	case STATE_OK:
		stepper_discard(&a->stepper);
		platform_audio_clear(a->plat);
		drain_audio(a);
		clear_crash(a);
		osd_show(a, "Loaded slot %d", a->slot);
		log_verbose("State file: %s", path);
		return true;
	case STATE_MISSING:
		osd_show(a, "Empty slot %d", a->slot);
		log_verbose("No state file: %s", path);
		return false;
	default:
		osd_show(a, "Bad state %d", a->slot);
		log_verbose("Invalid state file: %s", path);
		return false;
	}
}

static void toggle_pause(App *a) {
	if (!commit_pending(a)) {
		return;
	}
	a->paused = !a->paused;
	platform_audio_clear(a->plat);
	drain_audio(a);
	log_verbose(a->paused ? "Paused" : "Resumed");
}

static void step_job(void *arg) {
	StepJob *job = arg;

	switch (job->action) {
	case SC_STEP_FRAME:
		job->stalled = !stepper_frame(&job->app->stepper, &job->app->gb);
		break;
	case SC_STEP_INSTR:
		stepper_instruction(&job->app->stepper, &job->app->gb);
		break;
	default:
		stepper_mcycle(&job->app->stepper, &job->app->gb);
		break;
	}
}

static void step(App *a, ShortcutAction action) {
	if (!a->paused) {
		osd_show(a, "Pause to step");
		return;
	}
	if (!a->stepper.pending) {
		gb_set_joypad(&a->gb, platform_joypad(&a->settings.bindings));
	}

	StepJob job = {a, action, false};

	if (!guard_run(step_job, &job)) {
		enter_emulator_fault(a);
		return;
	}
	if (job.stalled) {
		enter_emulator_stall(a);
		return;
	}
	drain_audio(a);
	log_verbose("Step: PC %04X", (unsigned)a->gb.cpu.pc);
	check_guest_crash(a);
}

/* -- Input -- */

static void handle_shortcut(App *a, ShortcutAction action) {
	switch (action) {
	case SC_PAUSE:
		toggle_pause(a);
		break;
	case SC_SPEED:
		a->speed = (a->speed == 1) ? FAST_SPEED : 1;
		platform_audio_clear(a->plat);
		osd_show(a, "Speed %dx", a->speed);
		break;
	case SC_RESET:
		reset_game(a);
		break;
	case SC_SAVE_STATE:
		save_state(a);
		break;
	case SC_LOAD_STATE:
		load_state(a);
		break;
	case SC_STEP_FRAME:
	case SC_STEP_INSTR:
	case SC_STEP_MCYCLE:
		step(a, action);
		break;
	case SC_NONE:
	case SC_COUNT:
		break;
	}
}

/* Returns false when the app should quit. */
static bool handle_running_event(App *a, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_QUIT:
		return false;
	case PLATFORM_EV_THEME_CHANGED:
		ui_set_system_dark(&a->ui, platform_system_dark());
		return true;
	case PLATFORM_EV_BACK:
		if (!ev->repeat) {
			open_menu(a);
		}
		return true;
	default:
		break;
	}

	if (ev->key <= KEY_NONE) {
		return true;
	}

	int slot_key = platform_slot_for_key(ev->key);

	if (slot_key) {
		if (!ev->repeat) {
			a->slot = slot_key;
			osd_show(a, "Slot %d", a->slot);
		}
		return true;
	}

	ShortcutAction action = bindings_shortcut_for_key(&a->settings.bindings, ev->key);

	/* Only frame-stepping makes sense to repeat while the key is held. */
	if (action != SC_NONE && (!ev->repeat || action == SC_STEP_FRAME)) {
		handle_shortcut(a, action);
	}
	return true;
}

/* "Reset" on the crash screen: an emulator fault needs a full reload, a game crash a reset. */
static void crash_reset(App *a) {
	if (a->crash.active && a->crash.report.kind == CRASH_EMULATOR) {
		/* start_game() overwrites a->rom_path, so it can't be given a->rom_path itself. */
		char path[PATHS_MAX];

		snprintf(path, sizeof path, "%s", a->rom_path);
		if (!start_game(a, path)) {
			ui_notify(&a->ui, "Could not reload the ROM", true);
			return;
		}
		platform_audio_clear(a->plat);
	} else {
		reset_game(a);
	}
	close_menu(a);
}

static void resume_or_dismiss(App *a) {
	if (!a->crash.active) {
		close_menu(a);
	} else if (a->crash.report.kind == CRASH_GAME) {
		a->crash.ignored = true;
		close_menu(a);
	} else {
		ui_notify(&a->ui, "Unavailable after a fault", true);
	}
}

/* Returns false when the app should quit. */
static bool handle_ui_event(App *a, const PlatformEvent *ev) {
	switch (ui_handle_event(&a->ui, a->plat, ev)) {
	case UI_ACTION_QUIT:
		return false;
	case UI_ACTION_LOAD_ROM:
		if (start_game(a, ui_selected_rom(&a->ui))) {
			enter_game(a);
		} else {
			ui_notify(&a->ui, "Invalid or unsupported ROM", true);
		}
		break;
	case UI_ACTION_LOAD_STATE:
		if (load_state(a)) {
			close_menu(a);
		} else {
			ui_notify(&a->ui, "No valid state in this slot", true);
		}
		break;
	case UI_ACTION_RESUME:
		resume_or_dismiss(a);
		break;
	case UI_ACTION_RESET:
		crash_reset(a);
		break;
	case UI_ACTION_MAIN_MENU:
		back_to_main(a);
		break;
	case UI_ACTION_NONE:
		break;
	}
	return true;
}

/* -- Emulation loop -- */

/* --crash-test: breaks the machine (or the emulator) in the way the CLI asked for. */
static void inject_crash(App *a) {
	Gb *gb = &a->gb;

	log_verbose("Injecting test crash: %s", cli_crash_test_name(a->cli.crash_test));
	gb->cpu.ime = false;
	gb->cpu.halted = false;
	gb->cpu.ei_delay = 0;

	switch (a->cli.crash_test) {
	case CRASH_TEST_SEGV: {
		int *volatile bad = NULL;

		*bad = 1;
		break;
	}
	case CRASH_TEST_ABORT:
		abort();
	case CRASH_TEST_FPE: {
		volatile int zero = 0;
		volatile int result = 1 / zero;

		(void)result;
		break;
	}
	case CRASH_TEST_ILLEGAL: /* run an illegal opcode from WRAM */
		gb->wram[0] = 0xDD;
		gb->cpu.pc = MEM_WRAM_START;
		break;
	case CRASH_TEST_HALT: /* HALT with no interrupts enabled never wakes up */
		gb->wram[0] = 0x76;
		gb->cpu.pc = MEM_WRAM_START;
		gb->ie = 0;
		break;
	case CRASH_TEST_NONE:
	case CRASH_TEST_COUNT:
		break;
	}
}

static void frame_job(void *arg) {
	FrameJob *job = arg;
	App *a = job->app;

	if (a->crash.inject_pending) {
		a->crash.inject_pending = false;
		inject_crash(a);
	}
	for (int i = 0; i < a->speed; i++) {
		if (!gb_run_frame(&a->gb)) {
			job->complete = false;
			return;
		}
	}
}

static void run_emulation(App *a) {
	FrameJob job = {a, true};

	a->frames++;
	if (a->cli.crash_test != CRASH_TEST_NONE && a->frames == CRASH_TEST_FRAME &&
		!a->crash.test_fired) {
		a->crash.test_fired = true;
		a->crash.inject_pending = true;
	}

	gb_set_joypad(&a->gb, platform_joypad(&a->settings.bindings));
	if (!guard_run(frame_job, &job)) {
		enter_emulator_fault(a);
		return;
	}
	if (!job.complete) {
		enter_emulator_stall(a);
		return;
	}

	push_audio(a);
	if (a->frames % AUTOSAVE_FRAMES == 0) {
		cartridge_flush_battery(a->cart);
	}
	stats_tick(a);
	check_guest_crash(a);
}

static void update_cursor(App *a) {
	bool want = menu_has_focus(a);

	if (want != a->cursor_visible) {
		platform_show_cursor(want);
		a->cursor_visible = want;
	}
}

static void draw(App *a) {
	if (a->state == APP_MENU) {
		ui_draw(&a->ui, a->plat);
		return;
	}

	if (!a->paused && !a->menu_open) {
		run_emulation(a);
	} else {
		stats_reset(a);
	}
	platform_set_background(a->plat, COLOR_BLACK);
	platform_draw_frame(a->plat, gb_framebuffer(&a->gb));
	if (a->menu_open) {
		ui_draw(&a->ui, a->plat);
	} else {
		draw_hud(a);
	}
}

/* -- Startup -- */

static void log_startup(const App *a) {
	char dir[PATHS_MAX];

	log_verbose("gboyc %s", GBOYC_VERSION);
	if (paths_dir(PATHS_CONFIG, dir, sizeof dir)) {
		log_verbose("Config directory: %s", dir);
	}
	if (paths_dir(PATHS_DATA, dir, sizeof dir)) {
		log_verbose("Data directory: %s", dir);
	}
	log_verbose("Boot ROM in settings: %s", a->settings.boot_rom_enabled ? "on" : "off");
}

static bool load_boot_override(App *a) {
	if (!a->cli.boot_rom) {
		return true;
	}

	BootResult result = boot_load_file(a->cli.boot_rom, a->boot_override.rom);

	if (result != BOOT_OK) {
		log_error("boot ROM '%s': %s", a->cli.boot_rom, boot_result_text(result));
		return false;
	}
	a->boot_override.loaded = true;
	return true;
}

/* Applies --paused and --state to the ROM that was loaded from the command line. */
static void start_from_cli(App *a) {
	enter_game(a);
	a->paused = a->cli.paused;
	if (a->paused) {
		log_verbose("Starting paused (--paused)");
	}
	if (a->cli.load_state_slot) {
		a->slot = a->cli.load_state_slot;
		if (!load_state(a)) {
			log_error("warning: slot %d has no valid state", a->slot);
		}
	}
}

int main(int argc, char **argv) {
	switch (cli_parse(&app.cli, argc, argv)) {
	case CLI_EXIT_OK:
		return EXIT_SUCCESS;
	case CLI_EXIT_FAIL:
		return EXIT_FAILURE;
	case CLI_RUN:
		break;
	}

	log_set_verbose(app.cli.verbose);
	guard_install();
	settings_load(&app.settings);
	log_startup(&app);

	if (!load_boot_override(&app)) {
		return EXIT_FAILURE;
	}
	if (app.cli.rom && !start_game(&app, app.cli.rom)) {
		log_error("cannot load ROM '%s'", app.cli.rom);
		return EXIT_FAILURE;
	}

	app.plat = platform_create("gboyc", app.cli.scale, !app.cli.mute);
	if (!app.plat) {
		unload_game(&app);
		return EXIT_FAILURE;
	}
	log_verbose("Window scale: %dx, audio %s", app.cli.scale, app.cli.mute ? "off" : "on");

	ui_init(&app.ui, &app.settings);
	if (app.cart) {
		start_from_cli(&app);
	}

	bool running = true;

	while (running) {
		PlatformEvent ev;

		while (platform_next_event(app.plat, &ev)) {
			bool keep_running =
				menu_has_focus(&app) ? handle_ui_event(&app, &ev) : handle_running_event(&app, &ev);

			if (!keep_running) {
				running = false;
			}
		}

		update_cursor(&app);
		draw(&app);
		platform_flip(app.plat);
		platform_frame_limit(app.plat);
	}

	log_verbose("Exiting");
	unload_game(&app);
	stepper_free(&app.stepper);
	platform_destroy(app.plat);
	return EXIT_SUCCESS;
}

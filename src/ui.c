#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "bindings.h"
#include "boot.h"
#include "palette.h"
#include "theme.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* -- Layout -- */

#define SCALE_HERO	  5
#define SCALE_TITLE	  3
#define SCALE_BODY	  2
#define SCALE_CAPTION 1

#define BODY_TEXT_H (PLATFORM_GLYPH_H * SCALE_BODY)

#define MARGIN_X	  64
#define CONTENT_W	  (UI_VIEW_W - 2 * MARGIN_X)
#define CONTENT_RIGHT (MARGIN_X + CONTENT_W)

/* Header: logo mark, screen title, "GBOYC" tag, divider. */
#define HEADER_MARK_Y	 28
#define HEADER_MARK_CELL 12
#define HEADER_TITLE_X	 (MARGIN_X + 42)
#define HEADER_TITLE_Y	 30
#define HEADER_TAG_Y	 34
#define HEADER_LINE_Y	 72
#define SUBHEADER_Y		 92 /* small caption under the header (controls/shortcuts) */

/* Main-menu hero block. */
#define HERO_MARK_Y		56
#define HERO_MARK_CELL	24
#define HERO_TITLE_Y	124
#define HERO_SUBTITLE_Y 180

/* Standard list rows. */
#define LIST_Y		112
#define HERO_LIST_Y 250
#define ROW_H		40
#define ROW_PITCH	48
#define ROW_PAD		16

/* Controls / shortcuts rows are denser and carry key cells. */
#define PAD_ROW_H		24
#define PAD_ROW_PITCH	28
#define PAD_CELL_W		168
#define SHORTCUT_CELL_X (CONTENT_RIGHT - ROW_PAD - PAD_CELL_W)

/* Controller test screen. */
#define PAD_TEST_BACK_Y 434
#define TEST_TITLE_Y	112
#define TEST_BOX_Y		124 /* the three direction squares */
#define TEST_BOX_SIZE	136
#define TEST_BOX_COUNT	3
#define TEST_DOT		12
#define TEST_READ_Y		268
#define TEST_VALUES_Y	288
#define TEST_LOWER_Y	310 /* headings of the Game Boy diagram and the button list */
#define TEST_GB_Y		322
#define TEST_GB_W		240
#define TEST_GB_H		100
#define TEST_CHIP_X		(MARGIN_X + TEST_GB_W + 8)
#define TEST_CHIP_W		84
#define TEST_CHIP_H		16
#define TEST_CHIP_GAP	6
#define TEST_CHIP_PITCH 20
#define TEST_CHIP_COLS	3

/* Footer: status bar, divider, key hints. */
#define STATUS_Y	  480
#define STATUS_H	  32
#define STATUS_DOT	  8
#define STATUS_DOT_X  12
#define STATUS_TEXT_X 28
#define FOOTER_LINE_Y 524
#define HINT_Y		  542
#define HINT_KEY_H	  20
#define HINT_KEY_PAD  6
#define HINT_GAP	  16

/* "Load ROM" drop screen. */
#define DROP_X		96
#define DROP_Y		176
#define DROP_W		(UI_VIEW_W - 2 * DROP_X)
#define DROP_H		176
#define DROP_TEXT_Y 238
#define DROP_HINT_Y 276
#define DASH_LEN	8
#define DASH_PITCH	16
#define DASH_THICK	2

/* Crash screen: report card above the action list. */
#define CRASH_CARD_Y	 92
#define CRASH_CARD_H	 160
#define CRASH_DOT_Y		 16
#define CRASH_REASON_Y	 12
#define CRASH_REASON_X	 (ROW_PAD + 20)
#define CRASH_LINES_Y	 48
#define CRASH_LINE_PITCH 16
#define CRASH_LIST_Y	 268
#define CRASH_PITCH		 40
#define CRASH_ROW_H		 32

#define OVERLAY_ALPHA 0xB4000000u

/* In-game HUD chips. */
#define HUD_MARGIN 20
#define HUD_CHIP_H 32
#define HUD_PAD	   14
#define HUD_DOT	   8
#define HUD_ALPHA  0xE6000000u

/* -- Rows -- */

enum {
	MENU_LOAD_ROM,
	MENU_SETTINGS,
	MENU_QUIT,
	MENU_COUNT,
};

enum {
	SET_BOOT_ROM_ENABLED,
	SET_BOOT_ROM_FILE,
	SET_PLAYERS,
	SET_CONTROLS,
	SET_SHORTCUTS,
	SET_THEME,
	SET_BACK,
	SETTINGS_COUNT,
};

enum {
	PAUSE_CONTINUE,
	PAUSE_RESET,
	PAUSE_CONTROLS,
	PAUSE_SHORTCUTS,
	PAUSE_LOAD,
	PAUSE_MAIN,
	PAUSE_COUNT,
};

enum {
	CR_RESET,
	CR_LOAD_STATE,
	CR_LOAD_ROM,
	CR_MAIN,
	CR_DISMISS,
	CR_COUNT,
};

/*
 * The controls (keyboard / gamepad) and shortcuts screens share one layout: for controls a
 * "player" row and a "device" row, then "bind all", the bindable items, "reset to default"
 * and "back".
 */
#define BIND_ROW_PLAYER 0
#define BIND_ROW_DEVICE 1
#define BIND_HEAD_ROWS	2

typedef struct {
	const char *key;
	const char *label;
} Hint;

typedef struct {
	int y0;
	int pitch;
	int h;
	int count;
} ListGeom;

static const char *const MENU_LABELS[MENU_COUNT] = {"Load ROM", "Settings", "Quit"};
static const char *const PAUSE_LABELS[PAUSE_COUNT] = {
	"Continue", "Reset", "Controls", "Emulator shortcuts", "Load ROM", "Main menu"};
static const char *const PAUSE_VALUES[PAUSE_COUNT] = {NULL, NULL, ">", ">", NULL, NULL};
static const char *const CRASH_LABELS[CR_COUNT] = {"Reset", "Load state", "Load ROM", "Main menu",
												   "Dismiss"};

/* Row order of the Game Boy buttons on the controls and gamepad screens. */
static const JoyButton PAD_ORDER[JOY_BTN_COUNT] = {
	JOY_BTN_UP, JOY_BTN_DOWN, JOY_BTN_LEFT,	  JOY_BTN_RIGHT,
	JOY_BTN_A,	JOY_BTN_B,	  JOY_BTN_SELECT, JOY_BTN_START,
};

static const Hint HINTS_MENU[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}};
static const Hint HINTS_PAUSE[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}, {"ESC", "Resume"}};
static const Hint HINTS_SETTINGS[] = {
	{"UP/DOWN", "Select"}, {"L/R", "Adjust"}, {"ENTER", "Open"}, {"ESC", "Back"}};
static int hint_at(const Hint *hints, int count, int mx, int my);
static const Hint HINTS_PAD_TEST[] = {{"L/R", "Gamepad"}, {"ESC", "Back"}, {"BACK+START", "Back"}};
/* Controls: the hint bar doubles as buttons (HINT_EVENTS lists what each one sends). */
static const Hint HINTS_CONTROLS[] = {
	{"ESC", "Back"}, {"ENTER", "Rebind"}, {"DEL", "Clear"}, {"F1", "Test"}};
static const PlatformEventType HINT_EVENTS_CONTROLS[] = {PLATFORM_EV_BACK, PLATFORM_EV_CONFIRM,
														 PLATFORM_EV_CLEAR, PLATFORM_EV_TEST};
static const Hint HINTS_BIND[] = {{"ESC", "Back"}, {"ENTER", "Rebind"}, {"DEL", "Clear"}};
static const Hint HINTS_CAPTURE_KEY[] = {{"KEY", "Bind"}, {"ESC", "Cancel"}};
static const Hint HINTS_CAPTURE_BUTTON[] = {{"BTN", "Bind"}, {"ESC", "Cancel"}};
/* "Bind all": DEL leaves the current bind unmapped, ENTER stops and leaves the rest unmapped. */
static const Hint HINTS_BIND_ALL_KEY[] = {
	{"KEY", "Bind"}, {"DEL", "Unmapped"}, {"ENTER", "Finish"}, {"ESC", "Cancel"}};
static const Hint HINTS_BIND_ALL_BUTTON[] = {
	{"BTN", "Bind"}, {"DEL", "Unmapped"}, {"ENTER", "Finish"}, {"ESC", "Cancel"}};
static const Hint HINTS_CRASH[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}};

/* -- Binding screens: what each one edits -- */

static BindKind bind_kind(UiScreen screen) {
	switch (screen) {
	case UI_SCREEN_GAMEPAD:
		return BIND_GAMEPAD;
	case UI_SCREEN_SHORTCUTS:
		return BIND_SHORTCUT;
	default:
		return BIND_PAD;
	}
}

static int bind_item_count(UiScreen screen) {
	return bind_kind(screen) == BIND_SHORTCUT ? SC_COUNT : JOY_BTN_COUNT;
}

/* Keyboard and gamepad are two tabs of one player's controls, switched on the first row. */
static bool has_device_row(UiScreen screen) {
	return screen == UI_SCREEN_KEYBOARD || screen == UI_SCREEN_GAMEPAD;
}

static int bind_all_row(UiScreen screen) {
	return has_device_row(screen) ? BIND_HEAD_ROWS : 0;
}

static int bind_first_row(UiScreen screen) {
	return bind_all_row(screen) + 1;
}

static int bind_reset_row(UiScreen screen) {
	return bind_first_row(screen) + bind_item_count(screen);
}

static int bind_row_count(UiScreen screen) {
	return bind_reset_row(screen) + 2;
}

static int bind_back_row(UiScreen screen) {
	return bind_reset_row(screen) + 1;
}

static bool row_is_item(UiScreen screen, int row) {
	return row >= bind_first_row(screen) && row < bind_reset_row(screen);
}

/* Index into Bindings (a JoyButton or a ShortcutAction) for a row. */
static int bind_index(UiScreen screen, int row) {
	int item = row - bind_first_row(screen);

	if (bind_kind(screen) == BIND_SHORTCUT) {
		return item;
	}
	return PAD_ORDER[item];
}

static const char *bind_label(UiScreen screen, int index) {
	switch (bind_kind(screen)) {
	case BIND_SHORTCUT:
		return bindings_shortcut_label((ShortcutAction)index);
	default:
		return bindings_pad_label((JoyButton)index);
	}
}

/* Shortcuts are shared by both players and stored with player 0. */
static int bind_player(const Ui *ui) {
	return ui->screen == UI_SCREEN_SHORTCUTS ? 0 : ui->player;
}

static ListGeom geom(UiScreen screen) {
	switch (screen) {
	case UI_SCREEN_MENU:
		return (ListGeom){HERO_LIST_Y, ROW_PITCH, ROW_H, MENU_COUNT};
	case UI_SCREEN_SETTINGS:
		return (ListGeom){LIST_Y, ROW_PITCH, ROW_H, SETTINGS_COUNT};
	case UI_SCREEN_KEYBOARD:
	case UI_SCREEN_GAMEPAD:
	case UI_SCREEN_SHORTCUTS:
		return (ListGeom){LIST_Y, PAD_ROW_PITCH, PAD_ROW_H, bind_row_count(screen)};
	case UI_SCREEN_PAD_TEST:
		return (ListGeom){PAD_TEST_BACK_Y, ROW_PITCH, ROW_H, 1};
	case UI_SCREEN_PAUSE:
		return (ListGeom){LIST_Y, ROW_PITCH, ROW_H, PAUSE_COUNT};
	case UI_SCREEN_CRASH:
		return (ListGeom){CRASH_LIST_Y, CRASH_PITCH, CRASH_ROW_H, CR_COUNT};
	default:
		return (ListGeom){0, 1, 0, 0};
	}
}

/* -- State helpers -- */

static void set_status(Ui *ui, const char *text, bool warn) {
	snprintf(ui->status, sizeof ui->status, "%s", text);
	ui->status_warn = warn;
}

static void save_settings(Ui *ui) {
	if (!settings_save(ui->settings)) {
		set_status(ui, "Could not save settings", true);
	}
}

/* Switches screen, remembering the cursor row of the one being left. */
static void go(Ui *ui, UiScreen screen) {
	ui->sel[ui->screen] = ui->row;
	ui->screen = screen;
	ui->row = ui->sel[screen];
	ui->status[0] = '\0';
}

/* Like go(), but Back returns to the screen we came from. */
static void go_child(Ui *ui, UiScreen screen) {
	ui->parent = ui->screen;
	go(ui, screen);
}

static void move(Ui *ui, int dir) {
	int n = geom(ui->screen).count;

	if (n > 0) {
		ui->row = (ui->row + n + dir) % n;
	}
}

/* Handles UP/DOWN; returns true if the event was a navigation key. */
static bool list_navigate(Ui *ui, PlatformEventType type) {
	if (type == PLATFORM_EV_UP) {
		move(ui, -1);
		return true;
	}
	if (type == PLATFORM_EV_DOWN) {
		move(ui, 1);
		return true;
	}
	return false;
}

static void key_label(int scancode, char *out, size_t size) {
	const char *name = scancode != KEY_NONE ? platform_key_name(scancode) : NULL;

	if (!name || !*name) {
		snprintf(out, size, "-");
	} else if (strncmp(name, "Left ", 5) == 0) {
		snprintf(out, size, "L-%.7s", name + 5);
	} else if (strncmp(name, "Right ", 6) == 0) {
		snprintf(out, size, "R-%.7s", name + 6);
	} else {
		snprintf(out, size, "%.10s", name);
	}
}

/* Copies in to out, shortening it to max characters with "..." if needed. */
static void ellipsize(char *out, size_t size, const char *in, size_t max) {
	if (strlen(in) <= max) {
		snprintf(out, size, "%s", in);
	} else {
		snprintf(out, size, "%.*s...", (int)max - 3, in);
	}
}

/* Clears everything except the settings pointer and starts on the given screen. */
static void reset(Ui *ui, Settings *settings, UiScreen screen, bool in_game) {
	bool prefer_gamepad = ui->prefer_gamepad;

	memset(ui, 0, sizeof *ui);
	ui->prefer_gamepad = prefer_gamepad;
	ui->settings = settings;
	ui->system_dark = platform_system_dark();
	ui->screen = screen;
	ui->in_game = in_game;
}

void ui_init(Ui *ui, Settings *settings) {
	reset(ui, settings, UI_SCREEN_MENU, false);
}

void ui_open_pause(Ui *ui) {
	reset(ui, ui->settings, UI_SCREEN_PAUSE, true);
}

void ui_open_crash(Ui *ui, const CrashReport *report) {
	reset(ui, ui->settings, UI_SCREEN_CRASH, true);
	ui->crash = *report;
}

void ui_notify(Ui *ui, const char *text, bool warn) {
	set_status(ui, text, warn);
}

void ui_set_system_dark(Ui *ui, bool dark) {
	ui->system_dark = dark;
}

const char *ui_selected_rom(const Ui *ui) {
	return ui->rom_path;
}

/* -- Actions -- */

static void open_settings(Ui *ui) {
	go(ui, UI_SCREEN_SETTINGS);
	ui->boot_rom_installed = boot_is_installed(ui->settings->boot_rom_file);
}

static void open_controls(Ui *ui) {
	go_child(ui, ui->prefer_gamepad ? UI_SCREEN_GAMEPAD : UI_SCREEN_KEYBOARD);
	ui->capturing = false;
}

/* Flips between the keyboard and gamepad binds of the same player. */
static void switch_device(Ui *ui) {
	ui->sel[ui->screen] = ui->row;
	ui->screen = ui->screen == UI_SCREEN_KEYBOARD ? UI_SCREEN_GAMEPAD : UI_SCREEN_KEYBOARD;
	ui->prefer_gamepad = ui->screen == UI_SCREEN_GAMEPAD;
	ui->status[0] = '\0';
}

/* Flips between Player 1's and Player 2's binds on the same device. */
static void switch_player(Ui *ui) {
	ui->player = (ui->player + 1) % MAX_PLAYERS;
	ui->status[0] = '\0';
}

static void open_pad_test(Ui *ui, Platform *p) {
	int count = platform_gamepad_count(p);

	ui->test_pad = ui->player < count ? ui->player : 0;
	ui->sel[ui->screen] = ui->row;
	ui->screen = UI_SCREEN_PAD_TEST;
	ui->row = 0;
	ui->status[0] = '\0';
	platform_capture_input(p, PLATFORM_CAPTURE_MONITOR);
}

static void open_shortcuts(Ui *ui) {
	go_child(ui, UI_SCREEN_SHORTCUTS);
	ui->capturing = false;
}

static void open_dialog(Ui *ui, Platform *p, PlatformDialog kind) {
	ui->dialog_open = true;
	ui->dialog_kind = kind;
	platform_open_file_dialog(p, kind);
}

static void toggle_boot_rom(Ui *ui) {
	if (!ui->boot_rom_installed) {
		set_status(ui, "No boot ROM found", true);
		return;
	}

	ui->settings->boot_rom_enabled = !ui->settings->boot_rom_enabled;
	ui->status[0] = '\0';
	save_settings(ui);
}

static void cycle_theme(Ui *ui, int dir) {
	ui->settings->theme = (ThemeMode)(((int)ui->settings->theme + THEME_COUNT + dir) % THEME_COUNT);
	save_settings(ui);
}

static void toggle_players(Ui *ui) {
	ui->settings->players = ui->settings->players == 1 ? 2 : 1;
	save_settings(ui);
}

static void adjust_setting(Ui *ui, int dir) {
	if (ui->row == SET_BOOT_ROM_ENABLED) {
		toggle_boot_rom(ui);
	} else if (ui->row == SET_PLAYERS) {
		toggle_players(ui);
	} else if (ui->row == SET_THEME) {
		cycle_theme(ui, dir);
	}
}

static void import_boot_rom(Ui *ui, const char *path) {
	char name[SETTINGS_NAME_MAX];
	BootResult result = boot_import(path, name, sizeof name);

	if (result != BOOT_OK) {
		set_status(ui, boot_result_text(result), true);
		return;
	}

	snprintf(ui->settings->boot_rom_file, sizeof ui->settings->boot_rom_file, "%s", name);
	ui->settings->boot_rom_enabled = true;
	ui->boot_rom_installed = true;
	set_status(ui, "Boot ROM imported", false);
	save_settings(ui);
}

/* -- Key / button capture (controls, gamepad and shortcuts screens) -- */

static void set_capture(Ui *ui, Platform *p, PlatformCapture mode) {
	ui->capturing = mode != PLATFORM_CAPTURE_OFF;
	platform_capture_input(p, mode);
}

static void stop_capture(Ui *ui, Platform *p) {
	ui->bind_all = false;
	set_capture(ui, p, PLATFORM_CAPTURE_OFF);
}

static void prompt_bind(Ui *ui) {
	const char *what = bind_kind(ui->screen) == BIND_GAMEPAD ? "button" : "key";
	const char *label = bind_label(ui->screen, bind_index(ui->screen, ui->row));
	char text[sizeof ui->status];

	if (ui->bind_all) {
		snprintf(text, sizeof text, "%d/%d Press %s: %s", ui->row, bind_item_count(ui->screen),
				 what, label);
	} else {
		snprintf(text, sizeof text, "Press %s: %s", what, label);
	}
	set_status(ui, text, false);
}

static void start_capture(Ui *ui, Platform *p) {
	set_capture(ui, p, PLATFORM_CAPTURE_SINGLE);
	prompt_bind(ui);
}

/* Starts at the first item with every bind of this screen cleared. */
static void start_bind_all(Ui *ui, Platform *p) {
	UiScreen screen = ui->screen;

	ui->bind_backup = ui->settings->bindings;
	for (int row = bind_first_row(screen); row < bind_reset_row(screen); row++) {
		bindings_set(&ui->settings->bindings, bind_kind(screen), bind_player(ui),
					 bind_index(screen, row), 0);
	}
	ui->bind_all = true;
	ui->row = bind_first_row(screen);
	set_capture(ui, p, PLATFORM_CAPTURE_SEQUENCE);
	prompt_bind(ui);
}

/* Keeps what was bound so far; the remaining items stay unmapped. */
static void finish_bind_all(Ui *ui, Platform *p, const char *status) {
	stop_capture(ui, p);
	ui->row = bind_all_row(ui->screen);
	set_status(ui, status, false);
	save_settings(ui);
}

static void cancel_bind_all(Ui *ui, Platform *p) {
	ui->settings->bindings = ui->bind_backup;
	stop_capture(ui, p);
	ui->row = bind_all_row(ui->screen);
	set_status(ui, "Cancelled, nothing changed", false);
}

static void bind_all_next(Ui *ui, Platform *p) {
	ui->row++;
	if (row_is_item(ui->screen, ui->row)) {
		prompt_bind(ui);
	} else {
		finish_bind_all(ui, p, "All binds set");
	}
}

static void bind_captured(Ui *ui, Platform *p, const PlatformEvent *ev) {
	BindKind kind = bind_kind(ui->screen);
	bool is_gamepad = kind == BIND_GAMEPAD;
	int index = bind_index(ui->screen, ui->row);
	char msg[sizeof ui->status];

	/* A key press on the gamepad screen (or the reverse) is not an answer to the prompt. */
	if ((ev->type == PLATFORM_EV_GAMEPAD_CAPTURED) != is_gamepad) {
		return;
	}
	if (ev->key == KEY_NONE ||
		!(is_gamepad ? platform_gamepad_code_valid(ev->key) : platform_key_valid(ev->key))) {
		return;
	}
	if (bindings_conflict(&ui->settings->bindings, kind, bind_player(ui), index, ev->key, msg,
						  sizeof msg)) {
		set_status(ui, msg, true);
		return;
	}

	bindings_set(&ui->settings->bindings, kind, bind_player(ui), index, ev->key);
	if (ui->bind_all) {
		bind_all_next(ui, p);
		return;
	}
	stop_capture(ui, p);
	set_status(ui, is_gamepad ? "Button bound" : "Key bound", false);
	save_settings(ui);
}

/* While waiting for a press only the captured input and the control keys matter. */
static bool capture_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (!ui->capturing) {
		return false;
	}

	switch (ev->type) {
	case PLATFORM_EV_KEY_CAPTURED:
	case PLATFORM_EV_GAMEPAD_CAPTURED:
		bind_captured(ui, p, ev);
		break;
	case PLATFORM_EV_BACK:
		if (ui->bind_all) {
			cancel_bind_all(ui, p);
		} else {
			stop_capture(ui, p);
			set_status(ui, "", false);
		}
		break;
	case PLATFORM_EV_CONFIRM: /* only sent while binding everything */
		finish_bind_all(ui, p, "Finished, the rest is unmapped");
		break;
	case PLATFORM_EV_CLEAR: /* only sent while binding everything */
		bindings_set(&ui->settings->bindings, bind_kind(ui->screen), bind_player(ui),
					 bind_index(ui->screen, ui->row), 0);
		bind_all_next(ui, p);
		break;
	default:
		break;
	}
	return true;
}

static void reset_binds(Ui *ui) {
	switch (bind_kind(ui->screen)) {
	case BIND_GAMEPAD:
		platform_default_gamepad(&ui->settings->bindings, ui->player);
		break;
	case BIND_SHORTCUT:
		platform_default_shortcuts(&ui->settings->bindings);
		break;
	default:
		platform_default_pad(&ui->settings->bindings, ui->player);
		break;
	}
	set_status(ui, "Defaults restored", false);
	save_settings(ui);
}

static void bind_confirm(Ui *ui, Platform *p) {
	if (has_device_row(ui->screen) && ui->row == BIND_ROW_PLAYER) {
		switch_player(ui);
	} else if (has_device_row(ui->screen) && ui->row == BIND_ROW_DEVICE) {
		switch_device(ui);
	} else if (ui->row == bind_all_row(ui->screen)) {
		start_bind_all(ui, p);
	} else if (ui->row == bind_reset_row(ui->screen)) {
		reset_binds(ui);
	} else if (ui->row == bind_back_row(ui->screen)) {
		go(ui, ui->parent);
	} else {
		start_capture(ui, p);
	}
}

static void bind_clear(Ui *ui) {
	if (!row_is_item(ui->screen, ui->row)) {
		return;
	}

	bindings_set(&ui->settings->bindings, bind_kind(ui->screen), bind_player(ui),
				 bind_index(ui->screen, ui->row), 0);
	set_status(ui, "Cleared", false);
	save_settings(ui);
}

/* -- Per-screen events -- */

static void bind_screen_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (capture_event(ui, p, ev)) {
		return;
	}
	if (list_navigate(ui, ev->type)) {
		return;
	}

	switch (ev->type) {
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
		if (has_device_row(ui->screen) && ui->row == BIND_ROW_PLAYER) {
			switch_player(ui);
		} else if (has_device_row(ui->screen) && ui->row == BIND_ROW_DEVICE) {
			switch_device(ui);
		}
		break;
	case PLATFORM_EV_CONFIRM:
		bind_confirm(ui, p);
		break;
	case PLATFORM_EV_TEST:
		if (has_device_row(ui->screen)) {
			open_pad_test(ui, p);
		}
		break;
	case PLATFORM_EV_KEY:
		if (has_device_row(ui->screen) && ev->key == platform_test_key()) {
			open_pad_test(ui, p);
		}
		break;
	case PLATFORM_EV_CLEAR:
		bind_clear(ui);
		break;
	case PLATFORM_EV_BACK:
		go(ui, ui->parent);
		break;
	default:
		break;
	}
}

static void settings_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (list_navigate(ui, ev->type)) {
		return;
	}

	switch (ev->type) {
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
		adjust_setting(ui, ev->type == PLATFORM_EV_RIGHT ? 1 : -1);
		break;
	case PLATFORM_EV_CONFIRM:
		switch (ui->row) {
		case SET_BOOT_ROM_ENABLED:
			toggle_boot_rom(ui);
			break;
		case SET_BOOT_ROM_FILE:
			open_dialog(ui, p, PLATFORM_DIALOG_BOOT_ROM);
			break;
		case SET_PLAYERS:
			toggle_players(ui);
			break;
		case SET_CONTROLS:
			open_controls(ui);
			break;
		case SET_SHORTCUTS:
			open_shortcuts(ui);
			break;
		case SET_THEME:
			cycle_theme(ui, 1);
			break;
		default:
			go(ui, UI_SCREEN_MENU);
			break;
		}
		break;
	case PLATFORM_EV_BACK:
		go(ui, UI_SCREEN_MENU);
		break;
	default:
		break;
	}
}

static UiAction menu_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (list_navigate(ui, ev->type) || ev->type != PLATFORM_EV_CONFIRM) {
		return UI_ACTION_NONE;
	}

	switch (ui->row) {
	case MENU_QUIT:
		return UI_ACTION_QUIT;
	case MENU_SETTINGS:
		open_settings(ui);
		break;
	default:
		open_dialog(ui, p, PLATFORM_DIALOG_ROM);
		break;
	}
	return UI_ACTION_NONE;
}

static UiAction pause_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (list_navigate(ui, ev->type)) {
		return UI_ACTION_NONE;
	}
	if (ev->type == PLATFORM_EV_BACK) {
		return UI_ACTION_RESUME;
	}
	if (ev->type != PLATFORM_EV_CONFIRM) {
		return UI_ACTION_NONE;
	}

	switch (ui->row) {
	case PAUSE_CONTINUE:
		return UI_ACTION_RESUME;
	case PAUSE_RESET:
		return UI_ACTION_RESET;
	case PAUSE_CONTROLS:
		open_controls(ui);
		break;
	case PAUSE_SHORTCUTS:
		open_shortcuts(ui);
		break;
	case PAUSE_LOAD:
		open_dialog(ui, p, PLATFORM_DIALOG_ROM);
		break;
	default:
		return UI_ACTION_MAIN_MENU;
	}
	return UI_ACTION_NONE;
}

static UiAction crash_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (list_navigate(ui, ev->type) || ev->type != PLATFORM_EV_CONFIRM) {
		return UI_ACTION_NONE;
	}

	switch (ui->row) {
	case CR_RESET:
		return UI_ACTION_RESET;
	case CR_LOAD_STATE:
		return UI_ACTION_LOAD_STATE;
	case CR_LOAD_ROM:
		open_dialog(ui, p, PLATFORM_DIALOG_ROM);
		break;
	case CR_MAIN:
		return UI_ACTION_MAIN_MENU;
	default:
		return UI_ACTION_RESUME;
	}
	return UI_ACTION_NONE;
}

static void pad_test_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	int count = platform_gamepad_count(p);

	switch (ev->type) {
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
		if (count > 1) {
			ui->test_pad =
				(ui->test_pad + count + (ev->type == PLATFORM_EV_RIGHT ? 1 : -1)) % count;
		}
		break;
	case PLATFORM_EV_CONFIRM:
	case PLATFORM_EV_BACK:
	case PLATFORM_EV_GAMEPAD_MENU:
		platform_capture_input(p, PLATFORM_CAPTURE_OFF);
		go(ui, ui->prefer_gamepad ? UI_SCREEN_GAMEPAD : UI_SCREEN_KEYBOARD);
		break;
	default:
		break;
	}
}

static UiAction dispatch(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ui->screen) {
	case UI_SCREEN_MENU:
		return menu_event(ui, p, ev);
	case UI_SCREEN_PAUSE:
		return pause_event(ui, p, ev);
	case UI_SCREEN_ROM_DROP:
		if (ev->type == PLATFORM_EV_BACK) {
			go(ui, ui->parent);
		}
		break;
	case UI_SCREEN_SETTINGS:
		settings_event(ui, p, ev);
		break;
	case UI_SCREEN_KEYBOARD:
	case UI_SCREEN_GAMEPAD:
	case UI_SCREEN_SHORTCUTS:
		bind_screen_event(ui, p, ev);
		break;
	case UI_SCREEN_PAD_TEST:
		pad_test_event(ui, p, ev);
		break;
	case UI_SCREEN_CRASH:
		return crash_event(ui, p, ev);
	default:
		break;
	}
	return UI_ACTION_NONE;
}

/* -- File dialog results -- */

static bool boot_rom_dialog_active(const Ui *ui) {
	return ui->dialog_open && ui->dialog_kind == PLATFORM_DIALOG_BOOT_ROM;
}

static void close_dialog(Ui *ui) {
	ui->dialog_open = false;
	ui->dialog_kind = PLATFORM_DIALOG_ROM;
}

static UiAction file_chosen(Ui *ui, const PlatformEvent *ev) {
	if (boot_rom_dialog_active(ui)) {
		close_dialog(ui);
		import_boot_rom(ui, ev->path);
		return UI_ACTION_NONE;
	}

	/* A file dropped on the window only loads a ROM on screens that can start a game. */
	bool can_load_rom = ui->screen == UI_SCREEN_MENU || ui->screen == UI_SCREEN_ROM_DROP ||
						ui->screen == UI_SCREEN_PAUSE || ui->screen == UI_SCREEN_CRASH;

	if (!can_load_rom) {
		return UI_ACTION_NONE;
	}

	close_dialog(ui);
	snprintf(ui->rom_path, sizeof ui->rom_path, "%s", ev->path);
	return UI_ACTION_LOAD_ROM;
}

static void dialog_failed(Ui *ui) {
	bool was_boot_rom = boot_rom_dialog_active(ui);

	close_dialog(ui);
	if (was_boot_rom) {
		set_status(ui, "File dialog unavailable", true);
		return;
	}

	/* No native dialog: fall back to the drag-and-drop screen. */
	go_child(ui, UI_SCREEN_ROM_DROP);
}

/* -- Mouse -- */

/*
 * Moves the cursor to the row under (mx, my). Returns true if the point is on a row;
 * *on_cell tells whether it also hit something clickable.
 */
static bool pick_row(Ui *ui, int mx, int my, bool *on_cell) {
	ListGeom g = geom(ui->screen);
	int rel = my - g.y0;

	if (g.count == 0 || mx < MARGIN_X || mx >= CONTENT_RIGHT || rel < 0) {
		return false;
	}

	int idx = rel / g.pitch;

	if (idx >= g.count || rel % g.pitch >= g.h) {
		return false;
	}

	ui->row = idx;
	*on_cell = true;
	return true;
}

static bool is_user_input(PlatformEventType type) {
	switch (type) {
	case PLATFORM_EV_UP:
	case PLATFORM_EV_DOWN:
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
	case PLATFORM_EV_CONFIRM:
	case PLATFORM_EV_BACK:
	case PLATFORM_EV_CLEAR:
	case PLATFORM_EV_MOUSE_CLICK:
		return true;
	default:
		return false;
	}
}

UiAction ui_handle_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_QUIT:
		return UI_ACTION_QUIT;
	case PLATFORM_EV_THEME_CHANGED:
		ui->system_dark = platform_system_dark();
		return UI_ACTION_NONE;
	case PLATFORM_EV_FILE_CHOSEN:
		return file_chosen(ui, ev);
	case PLATFORM_EV_DIALOG_CANCELED:
		close_dialog(ui);
		return UI_ACTION_NONE;
	case PLATFORM_EV_DIALOG_FAILED:
		dialog_failed(ui);
		return UI_ACTION_NONE;
	default:
		break;
	}

	if (ui->dialog_open || ev->repeat) {
		return UI_ACTION_NONE;
	}
	if (!ui->capturing && is_user_input(ev->type)) {
		ui->status[0] = '\0';
	}

	bool on_cell = false;

	switch (ev->type) {
	case PLATFORM_EV_MOUSE_MOVE:
		if (!ui->capturing) {
			pick_row(ui, ev->mx, ev->my, &on_cell);
		}
		return UI_ACTION_NONE;
	case PLATFORM_EV_MOUSE_CLICK:
		if (!ui->capturing && has_device_row(ui->screen)) {
			int hint = hint_at(HINTS_CONTROLS, COUNT(HINTS_CONTROLS), ev->mx, ev->my);

			if (hint >= 0) {
				PlatformEvent click = {.type = HINT_EVENTS_CONTROLS[hint]};

				return dispatch(ui, p, &click);
			}
		}
		if (!ui->capturing && pick_row(ui, ev->mx, ev->my, &on_cell) && on_cell) {
			PlatformEvent confirm = {.type = PLATFORM_EV_CONFIRM};

			return dispatch(ui, p, &confirm);
		}
		return UI_ACTION_NONE;
	default:
		return dispatch(ui, p, ev);
	}
}

/* -- Drawing primitives -- */

static int text_w(const char *text, int scale) {
	return (int)strlen(text) * PLATFORM_GLYPH_W * scale;
}

static void text_right(Platform *p, int right, int y, int scale, uint32_t color, const char *text) {
	platform_draw_text(p, right - text_w(text, scale), y, scale, color, text);
}

static void text_center(Platform *p, int cx, int y, int scale, uint32_t color, const char *text) {
	platform_draw_text(p, cx - text_w(text, scale) / 2, y, scale, color, text);
}

/* Rectangle with the corners notched by 3px. */
static void fill_card(Platform *p, int x, int y, int w, int h, uint32_t color) {
	platform_fill_rect(p, x + 3, y, w - 6, h, color);
	platform_fill_rect(p, x + 1, y + 1, w - 2, h - 2, color);
	platform_fill_rect(p, x, y + 3, w, h - 6, color);
}

/* The four-shade logo: a 2x2 block of cells with a 2px gap. */
static void draw_mark(Platform *p, int x, int y, int cell) {
	platform_fill_rect(p, x, y, cell, cell, DMG_COLOR_0);
	platform_fill_rect(p, x + cell + 2, y, cell, cell, DMG_COLOR_1);
	platform_fill_rect(p, x, y + cell + 2, cell, cell, DMG_COLOR_2);
	platform_fill_rect(p, x + cell + 2, y + cell + 2, cell, cell, DMG_COLOR_3);
}

static int mark_width(int cell) {
	return 2 * cell + 2;
}

static void draw_header(Platform *p, const Theme *t, const char *title) {
	draw_mark(p, MARGIN_X, HEADER_MARK_Y, HEADER_MARK_CELL);
	platform_draw_text(p, HEADER_TITLE_X, HEADER_TITLE_Y, SCALE_TITLE, t->text, title);
	text_right(p, CONTENT_RIGHT, HEADER_TAG_Y, SCALE_BODY, t->dim, "GBOYC");
	platform_fill_rect(p, MARGIN_X, HEADER_LINE_Y, CONTENT_W, 2, t->line);
}

/* Width of one hint: the key cap plus its label. */
static int hint_w(const Hint *h) {
	return text_w(h->key, SCALE_CAPTION) + 3 * HINT_KEY_PAD + text_w(h->label, SCALE_CAPTION);
}

/* Index of the hint under (mx, my), or -1. Same layout as draw_hints. */
static int hint_at(const Hint *hints, int count, int mx, int my) {
	int x = MARGIN_X;

	if (my < HINT_Y - HINT_KEY_PAD || my >= HINT_Y - HINT_KEY_PAD + HINT_KEY_H) {
		return -1;
	}
	for (int i = 0; i < count; i++) {
		if (mx >= x && mx < x + hint_w(&hints[i])) {
			return i;
		}
		x += hint_w(&hints[i]) + HINT_GAP;
	}
	return -1;
}

static void draw_hints(Platform *p, const Theme *t, const Hint *hints, int count) {
	int x = MARGIN_X;

	platform_fill_rect(p, MARGIN_X, FOOTER_LINE_Y, CONTENT_W, 2, t->line);
	for (int i = 0; i < count; i++) {
		int key_w = text_w(hints[i].key, SCALE_CAPTION) + 2 * HINT_KEY_PAD;

		fill_card(p, x, HINT_Y - HINT_KEY_PAD, key_w, HINT_KEY_H, t->surface);
		platform_draw_text(p, x + HINT_KEY_PAD, HINT_Y, SCALE_CAPTION, t->text, hints[i].key);
		x += key_w + HINT_KEY_PAD;
		platform_draw_text(p, x, HINT_Y, SCALE_CAPTION, t->dim, hints[i].label);
		x += text_w(hints[i].label, SCALE_CAPTION) + HINT_GAP;
	}
}

static void draw_status(const Ui *ui, Platform *p, const Theme *t) {
	const char *text = ui->dialog_open ? "Waiting for file dialog..." : ui->status;
	bool warn = !ui->dialog_open && ui->status_warn;

	if (!text[0]) {
		return;
	}

	fill_card(p, MARGIN_X, STATUS_Y, CONTENT_W, STATUS_H, t->surface);
	platform_fill_rect(p, MARGIN_X + STATUS_DOT_X, STATUS_Y + (STATUS_H - STATUS_DOT) / 2,
					   STATUS_DOT, STATUS_DOT, warn ? t->warn : t->accent);
	platform_draw_text(p, MARGIN_X + STATUS_TEXT_X, STATUS_Y + (STATUS_H - BODY_TEXT_H) / 2,
					   SCALE_BODY, t->text, text);
}

/* Row background plus left-aligned label: shared by every list row. */
static void draw_label_row(Platform *p, const Theme *t, int y, int h, bool selected,
						   const char *label) {
	fill_card(p, MARGIN_X, y, CONTENT_W, h, selected ? t->accent : t->surface);
	platform_draw_text(p, MARGIN_X + ROW_PAD, y + (h - BODY_TEXT_H) / 2, SCALE_BODY,
					   selected ? t->on_accent : t->text, label);
}

static void draw_row(Platform *p, const Theme *t, int y, int h, bool selected, const char *label,
					 const char *value, bool adjustable) {
	draw_label_row(p, t, y, h, selected, label);

	if (value) {
		char buf[48];

		if (selected && adjustable) {
			snprintf(buf, sizeof buf, "< %s >", value);
		} else {
			snprintf(buf, sizeof buf, "%s", value);
		}
		text_right(p, CONTENT_RIGHT - ROW_PAD, y + (h - BODY_TEXT_H) / 2, SCALE_BODY,
				   selected ? t->on_accent : t->dim, buf);
	}
}

/* -- Screens -- */

static void draw_menu(const Ui *ui, Platform *p, const Theme *t) {
	int cx = UI_VIEW_W / 2;
	ListGeom g = geom(UI_SCREEN_MENU);

	draw_mark(p, cx - mark_width(HERO_MARK_CELL) / 2, HERO_MARK_Y, HERO_MARK_CELL);
	text_center(p, cx, HERO_TITLE_Y, SCALE_HERO, t->text, "GBOYC");
	text_center(p, cx, HERO_SUBTITLE_Y, SCALE_BODY, t->dim, "Game Boy emulator");

	for (int i = 0; i < MENU_COUNT; i++) {
		draw_row(p, t, g.y0 + i * g.pitch, g.h, i == ui->row, MENU_LABELS[i], NULL, false);
	}

	draw_status(ui, p, t);
	draw_hints(p, t, HINTS_MENU, COUNT(HINTS_MENU));
}

static void draw_pause(const Ui *ui, Platform *p, const Theme *t) {
	ListGeom g = geom(UI_SCREEN_PAUSE);

	draw_header(p, t, "Paused");
	for (int i = 0; i < PAUSE_COUNT; i++) {
		draw_row(p, t, g.y0 + i * g.pitch, g.h, i == ui->row, PAUSE_LABELS[i], PAUSE_VALUES[i],
				 false);
	}

	draw_status(ui, p, t);
	draw_hints(p, t, HINTS_PAUSE, COUNT(HINTS_PAUSE));
}

static void draw_crash(const Ui *ui, Platform *p, const Theme *t) {
	const CrashReport *r = &ui->crash;
	ListGeom g = geom(UI_SCREEN_CRASH);

	draw_header(p, t, r->title);
	fill_card(p, MARGIN_X, CRASH_CARD_Y, CONTENT_W, CRASH_CARD_H, t->surface);
	platform_fill_rect(p, MARGIN_X + ROW_PAD, CRASH_CARD_Y + CRASH_DOT_Y, STATUS_DOT, STATUS_DOT,
					   t->warn);
	platform_draw_text(p, MARGIN_X + CRASH_REASON_X, CRASH_CARD_Y + CRASH_REASON_Y, SCALE_BODY,
					   t->text, r->reason);
	for (int i = 0; i < r->line_count; i++) {
		platform_draw_text(p, MARGIN_X + ROW_PAD,
						   CRASH_CARD_Y + CRASH_LINES_Y + i * CRASH_LINE_PITCH, SCALE_CAPTION,
						   t->dim, r->lines[i]);
	}

	for (int i = 0; i < CR_COUNT; i++) {
		draw_row(p, t, g.y0 + i * g.pitch, g.h, i == ui->row, CRASH_LABELS[i], NULL, false);
	}

	draw_status(ui, p, t);
	draw_hints(p, t, HINTS_CRASH, COUNT(HINTS_CRASH));
}

static void draw_dashed_rect(Platform *p, int x, int y, int w, int h, uint32_t color) {
	for (int i = 0; i < w; i += DASH_PITCH) {
		int len = (w - i < DASH_LEN) ? w - i : DASH_LEN;

		platform_fill_rect(p, x + i, y, len, DASH_THICK, color);
		platform_fill_rect(p, x + i, y + h - DASH_THICK, len, DASH_THICK, color);
	}
	for (int i = 0; i < h; i += DASH_PITCH) {
		int len = (h - i < DASH_LEN) ? h - i : DASH_LEN;

		platform_fill_rect(p, x, y + i, DASH_THICK, len, color);
		platform_fill_rect(p, x + w - DASH_THICK, y + i, DASH_THICK, len, color);
	}
}

static void draw_rom_drop(Platform *p, const Theme *t) {
	int cx = UI_VIEW_W / 2;

	draw_header(p, t, "Load ROM");
	draw_dashed_rect(p, DROP_X, DROP_Y, DROP_W, DROP_H, t->line);
	text_center(p, cx, DROP_TEXT_Y, SCALE_BODY, t->text, "Drop the ROM file here");
	text_center(p, cx, DROP_HINT_Y, SCALE_CAPTION, t->dim, "Press ESC to go back");
}

static void draw_settings(const Ui *ui, Platform *p, const Theme *t) {
	char file[24];
	ListGeom g = geom(UI_SCREEN_SETTINGS);
	const Settings *s = ui->settings;

	draw_header(p, t, "Settings");

	for (int i = 0; i < SETTINGS_COUNT; i++) {
		const char *label = "Back";
		const char *value = NULL;
		bool adjustable = false;

		switch (i) {
		case SET_BOOT_ROM_ENABLED:
			label = "Boot ROM";
			value = !ui->boot_rom_installed ? "Unavailable" : (s->boot_rom_enabled ? "ON" : "OFF");
			adjustable = ui->boot_rom_installed;
			break;
		case SET_BOOT_ROM_FILE:
			label = "Boot ROM file";
			ellipsize(file, sizeof file, ui->boot_rom_installed ? s->boot_rom_file : "none", 14);
			value = file;
			break;
		case SET_PLAYERS:
			label = "Players";
			value = s->players == 2 ? "2 (linked)" : "1";
			adjustable = true;
			break;
		case SET_CONTROLS:
			label = "Controls";
			value = ">";
			break;
		case SET_SHORTCUTS:
			label = "Emulator shortcuts";
			value = ">";
			break;
		case SET_THEME:
			label = "Theme";
			value = theme_mode_name(s->theme);
			adjustable = true;
			break;
		default:
			break;
		}
		draw_row(p, t, g.y0 + i * g.pitch, g.h, i == ui->row, label, value, adjustable);
	}

	draw_status(ui, p, t);
	draw_hints(p, t, HINTS_SETTINGS, COUNT(HINTS_SETTINGS));
}

static void draw_key_cell(Platform *p, const Theme *t, int x, int y, bool selected, bool active,
						  const char *name) {
	uint32_t fg = selected ? t->on_accent : t->text;

	if (!selected && strcmp(name, "-") == 0) {
		fg = t->dim;
	}
	if (active) {
		fill_card(p, x, y + 3, PAD_CELL_W, PAD_ROW_H - 6, t->on_accent);
		fg = t->accent;
	}
	text_center(p, x + PAD_CELL_W / 2, y + (PAD_ROW_H - BODY_TEXT_H) / 2, SCALE_BODY, fg, name);
}

/* Text for a bound key / gamepad button, or "-" when unbound. */
static void bind_value_label(BindKind kind, int code, char *out, size_t size) {
	if (kind == BIND_GAMEPAD) {
		const char *name = platform_gamepad_button_name(code);

		snprintf(out, size, "%s", name ? name : "-");
	} else {
		key_label(code, out, size);
	}
}

static void draw_bind_cell(const Ui *ui, Platform *p, const Theme *t, int y, int index,
						   bool selected) {
	char name[16];

	bind_value_label(
		bind_kind(ui->screen),
		bindings_get(&ui->settings->bindings, bind_kind(ui->screen), bind_player(ui), index), name,
		sizeof name);
	if (ui->capturing && selected) {
		snprintf(name, sizeof name, "...");
	}
	draw_key_cell(p, t, SHORTCUT_CELL_X, y, selected, selected, name);
}

static void draw_bind_row(const Ui *ui, Platform *p, const Theme *t, int row) {
	UiScreen screen = ui->screen;
	ListGeom g = geom(screen);
	int y = g.y0 + row * g.pitch;
	bool selected = row == ui->row;

	if (has_device_row(screen) && row == BIND_ROW_PLAYER) {
		draw_row(p, t, y, g.h, selected, "Player", ui->player ? "Player 2" : "Player 1", true);
		return;
	}
	if (has_device_row(screen) && row == BIND_ROW_DEVICE) {
		draw_row(p, t, y, g.h, selected, "Device",
				 screen == UI_SCREEN_GAMEPAD ? "Gamepad" : "Keyboard", true);
		return;
	}
	if (!row_is_item(screen, row)) {
		const char *label = "Back";

		if (row == bind_all_row(screen)) {
			label = "Bind all";
		} else if (row == bind_reset_row(screen)) {
			label = "Reset to default";
		}
		draw_row(p, t, y, g.h, selected, label, NULL, false);
		return;
	}

	int index = bind_index(screen, row);

	draw_label_row(p, t, y, g.h, selected, bind_label(screen, index));
	draw_bind_cell(ui, p, t, y, index, selected);
}

static void draw_bind_caption(const Ui *ui, Platform *p, const Theme *t) {
	char text[64];
	bool warn = false;

	if (ui->screen == UI_SCREEN_SHORTCUTS) {
		snprintf(text, sizeof text, "Emulator actions. State slots: keys 1-9");
	} else if (ui->screen == UI_SCREEN_KEYBOARD) {
		snprintf(text, sizeof text, "One key per Game Boy button");
	} else {
		const char *name = platform_gamepad_name(p, ui->player);

		if (name) {
			char shown[32];

			ellipsize(shown, sizeof shown, name, 28);
			snprintf(text, sizeof text, "Gamepad %d: %s", ui->player + 1, shown);
		} else {
			snprintf(text, sizeof text, "No gamepad connected for Player %d", ui->player + 1);
			warn = true;
		}
	}
	platform_draw_text(p, MARGIN_X, SUBHEADER_Y, SCALE_CAPTION, warn ? t->warn : t->dim, text);
}

static void draw_bind_hints(const Ui *ui, Platform *p, const Theme *t) {
	bool button = ui->screen == UI_SCREEN_GAMEPAD;

	if (ui->capturing && ui->bind_all) {
		const Hint *hints = button ? HINTS_BIND_ALL_BUTTON : HINTS_BIND_ALL_KEY;

		draw_hints(p, t, hints, COUNT(HINTS_BIND_ALL_KEY));
	} else if (ui->capturing) {
		draw_hints(p, t, button ? HINTS_CAPTURE_BUTTON : HINTS_CAPTURE_KEY,
				   COUNT(HINTS_CAPTURE_KEY));
	} else if (has_device_row(ui->screen)) {
		draw_hints(p, t, HINTS_CONTROLS, COUNT(HINTS_CONTROLS));
	} else {
		draw_hints(p, t, HINTS_BIND, COUNT(HINTS_BIND));
	}
}

static void draw_bind_screen(const Ui *ui, Platform *p, const Theme *t) {
	draw_header(p, t, ui->screen == UI_SCREEN_SHORTCUTS ? "Shortcuts" : "Controls");
	draw_bind_caption(ui, p, t);
	for (int row = 0; row < bind_row_count(ui->screen); row++) {
		draw_bind_row(ui, p, t, row);
	}
	draw_status(ui, p, t);
	draw_bind_hints(ui, p, t);
}

/* A small labelled tag that lights up while the button is held. */
static void draw_chip(Platform *p, const Theme *t, int x, int y, bool on, const char *label) {
	fill_card(p, x, y, TEST_CHIP_W, TEST_CHIP_H, on ? t->accent : t->surface);
	text_center(p, x + TEST_CHIP_W / 2, y + (TEST_CHIP_H - PLATFORM_GLYPH_H) / 2, SCALE_CAPTION,
				on ? t->on_accent : t->dim, label);
}

/* "N", "SE", ... for a direction past the dead zone; "-" when centred. */
static const char *compass(int x, int y) {
	static const char *const NAMES[3][3] = {{"NW", "N", "NE"}, {"W", "-", "E"}, {"SW", "S", "SE"}};
	int cx = x > PLATFORM_STICK_DEADZONE ? 2 : (x < -PLATFORM_STICK_DEADZONE ? 0 : 1);
	int cy = y > PLATFORM_STICK_DEADZONE ? 2 : (y < -PLATFORM_STICK_DEADZONE ? 0 : 1);

	return NAMES[cy][cx];
}

/*
 * A square with a dot that follows the input (-32768..32767 on both axes). The darker inner
 * square is the dead zone: the dot only turns bright once the direction counts.
 */
static void draw_direction_box(Platform *p, const Theme *t, int bx, const char *title, int x,
							   int y) {
	int s = TEST_BOX_SIZE;
	int dead = s * PLATFORM_STICK_DEADZONE / 32768;
	int reach = (s - TEST_DOT) / 2;
	int cx = bx + s / 2;
	int cy = TEST_BOX_Y + s / 2;
	bool active = compass(x, y)[0] != '-';
	char text[32];

	text_center(p, cx, TEST_TITLE_Y, SCALE_CAPTION, t->dim, title);
	fill_card(p, bx, TEST_BOX_Y, s, s, t->surface);
	platform_fill_rect(p, cx - dead / 2, cy - dead / 2, dead, dead, t->bg);
	platform_fill_rect(p, bx + 6, cy - 1, s - 12, 2, t->line);
	platform_fill_rect(p, cx - 1, TEST_BOX_Y + 6, 2, s - 12, t->line);
	platform_fill_rect(p, cx + x * reach / 32768 - TEST_DOT / 2,
					   cy + y * reach / 32768 - TEST_DOT / 2, TEST_DOT, TEST_DOT,
					   active ? t->accent : t->dim);

	text_center(p, cx, TEST_READ_Y, SCALE_BODY, active ? t->text : t->dim, compass(x, y));
	snprintf(text, sizeof text, "x %+.2f  y %+.2f", x / 32768.0, y / 32768.0);
	text_center(p, cx, TEST_VALUES_Y, SCALE_CAPTION, t->dim, text);
}

/* The front of a Game Boy: each control lights up while the emulator reads it as pressed. */
static void draw_gb_diagram(Platform *p, const Theme *t, int x, int y, uint8_t mask) {
	const int arm = 22;
	int cx = x + 52;
	int cy = y + 44;
	uint32_t off = t->line;
	uint32_t on = t->accent;

	fill_card(p, x, y, TEST_GB_W, TEST_GB_H, t->surface);
	platform_fill_rect(p, cx - arm / 2, cy - arm / 2, arm, arm, off);
	platform_fill_rect(p, cx - arm / 2, cy - arm - arm / 2, arm, arm,
					   mask & JOY_MASK(JOY_BTN_UP) ? on : off);
	platform_fill_rect(p, cx - arm / 2, cy + arm / 2, arm, arm,
					   mask & JOY_MASK(JOY_BTN_DOWN) ? on : off);
	platform_fill_rect(p, cx - arm - arm / 2, cy - arm / 2, arm, arm,
					   mask & JOY_MASK(JOY_BTN_LEFT) ? on : off);
	platform_fill_rect(p, cx + arm / 2, cy - arm / 2, arm, arm,
					   mask & JOY_MASK(JOY_BTN_RIGHT) ? on : off);

	fill_card(p, x + 154, y + 48, 32, 32, mask & JOY_MASK(JOY_BTN_B) ? on : off);
	text_center(p, x + 170, y + 60, SCALE_BODY, mask & JOY_MASK(JOY_BTN_B) ? t->on_accent : t->dim,
				"B");
	fill_card(p, x + 194, y + 28, 32, 32, mask & JOY_MASK(JOY_BTN_A) ? on : off);
	text_center(p, x + 210, y + 40, SCALE_BODY, mask & JOY_MASK(JOY_BTN_A) ? t->on_accent : t->dim,
				"A");

	fill_card(p, x + 64, y + 80, 52, 14, mask & JOY_MASK(JOY_BTN_SELECT) ? on : off);
	text_center(p, x + 90, y + 83, SCALE_CAPTION,
				mask & JOY_MASK(JOY_BTN_SELECT) ? t->on_accent : t->dim, "SELECT");
	fill_card(p, x + 124, y + 80, 52, 14, mask & JOY_MASK(JOY_BTN_START) ? on : off);
	text_center(p, x + 150, y + 83, SCALE_CAPTION,
				mask & JOY_MASK(JOY_BTN_START) ? t->on_accent : t->dim, "START");
}

static void draw_pad_test(const Ui *ui, Platform *p, const Theme *t) {
	ListGeom g = geom(UI_SCREEN_PAD_TEST);
	int count = platform_gamepad_count(p);
	int pad = count ? ui->test_pad % count : 0;
	PlatformPadState st;
	char text[64];

	draw_header(p, t, "Test controller");

	if (!platform_gamepad_state(p, pad, &st)) {
		platform_draw_text(p, MARGIN_X, SUBHEADER_Y, SCALE_CAPTION, t->warn,
						   "No gamepad detected. Plug one in and it shows up here.");
	} else {
		/* Shown in this order; a trigger (axis >= 0) counts as pressed past a threshold. */
		static const struct {
			int button;
			int axis;
			const char *name;
		} CHIPS[] = {
			{PLATFORM_BTN_NORTH, -1, "North"},
			{PLATFORM_BTN_SOUTH, -1, "South"},
			{PLATFORM_BTN_WEST, -1, "West"},
			{PLATFORM_BTN_EAST, -1, "East"},
			{PLATFORM_BTN_BACK, -1, "Select"},
			{PLATFORM_BTN_START, -1, "Start"},
			{PLATFORM_BTN_GUIDE, -1, "Guide"},
			{PLATFORM_BTN_LEFT_BUMPER, -1, "L-Bumper"},
			{PLATFORM_BTN_RIGHT_BUMPER, -1, "R-Bumper"},
			{0, PLATFORM_AXIS_LEFT_TRIGGER, "L-Trigger"},
			{0, PLATFORM_AXIS_RIGHT_TRIGGER, "R-Trigger"},
			{PLATFORM_BTN_LEFT_STICK, -1, "L-Stick"},
			{PLATFORM_BTN_RIGHT_STICK, -1, "R-Stick"},
		};
		int players = ui->settings->players;
		int player = players == 2 ? pad : 0;
		int gap = (CONTENT_W - TEST_BOX_COUNT * TEST_BOX_SIZE) / (TEST_BOX_COUNT - 1);
		int dx = (st.buttons >> PLATFORM_BTN_DPAD_RIGHT & 1) -
				 (st.buttons >> PLATFORM_BTN_DPAD_LEFT & 1);
		int dy =
			(st.buttons >> PLATFORM_BTN_DPAD_DOWN & 1) - (st.buttons >> PLATFORM_BTN_DPAD_UP & 1);
		char shown[32];

		ellipsize(shown, sizeof shown, platform_gamepad_name(p, pad), 28);
		snprintf(text, sizeof text, "Gamepad %d of %d: %s", pad + 1, count, shown);
		platform_draw_text(p, MARGIN_X, SUBHEADER_Y, SCALE_CAPTION, t->dim, text);

		draw_direction_box(p, t, MARGIN_X, "D-PAD", dx * 32767, dy * 32767);
		draw_direction_box(p, t, MARGIN_X + TEST_BOX_SIZE + gap, "LEFT STICK",
						   st.axes[PLATFORM_AXIS_LEFT_X], st.axes[PLATFORM_AXIS_LEFT_Y]);
		draw_direction_box(p, t, MARGIN_X + 2 * (TEST_BOX_SIZE + gap), "RIGHT STICK",
						   st.axes[PLATFORM_AXIS_RIGHT_X], st.axes[PLATFORM_AXIS_RIGHT_Y]);

		if (players == 2 && pad >= MAX_PLAYERS) {
			snprintf(text, sizeof text, "EMULATOR INPUT: NOT USED WITH 2 PLAYERS");
		} else {
			snprintf(text, sizeof text, "EMULATOR INPUT: PLAYER %d", player + 1);
		}
		platform_draw_text(p, MARGIN_X, TEST_LOWER_Y, SCALE_CAPTION, t->dim, text);
		draw_gb_diagram(p, t, MARGIN_X, TEST_GB_Y,
						platform_joypad(p, &ui->settings->bindings, player, players));

		platform_draw_text(p, TEST_CHIP_X, TEST_LOWER_Y, SCALE_CAPTION, t->dim, "BUTTONS");
		for (int i = 0; i < COUNT(CHIPS); i++) {
			bool down = CHIPS[i].axis >= 0 ? st.axes[CHIPS[i].axis] > PLATFORM_TRIGGER_PRESSED
										   : (st.buttons >> CHIPS[i].button) & 1;

			draw_chip(p, t, TEST_CHIP_X + (i % TEST_CHIP_COLS) * (TEST_CHIP_W + TEST_CHIP_GAP),
					  TEST_GB_Y + (i / TEST_CHIP_COLS) * TEST_CHIP_PITCH, down, CHIPS[i].name);
		}
	}

	draw_row(p, t, g.y0, g.h, true, "Back", NULL, false);
	draw_status(ui, p, t);
	draw_hints(p, t, HINTS_PAD_TEST, COUNT(HINTS_PAD_TEST));
}

void ui_draw(const Ui *ui, Platform *p) {
	const Theme *t = theme_get(ui->settings->theme, ui->system_dark);

	if (ui->in_game) {
		/* Dim the running game behind the menu. */
		platform_fill_rect(p, 0, 0, UI_VIEW_W, UI_VIEW_H, (t->bg & 0x00FFFFFFu) | OVERLAY_ALPHA);
	} else {
		platform_set_background(p, t->bg);
		platform_clear(p, t->bg);
	}

	switch (ui->screen) {
	case UI_SCREEN_MENU:
		draw_menu(ui, p, t);
		break;
	case UI_SCREEN_PAUSE:
		draw_pause(ui, p, t);
		break;
	case UI_SCREEN_ROM_DROP:
		draw_rom_drop(p, t);
		break;
	case UI_SCREEN_SETTINGS:
		draw_settings(ui, p, t);
		break;
	case UI_SCREEN_KEYBOARD:
	case UI_SCREEN_GAMEPAD:
	case UI_SCREEN_SHORTCUTS:
		draw_bind_screen(ui, p, t);
		break;
	case UI_SCREEN_PAD_TEST:
		draw_pad_test(ui, p, t);
		break;
	case UI_SCREEN_CRASH:
		draw_crash(ui, p, t);
		break;
	default:
		break;
	}
}

/* -- In-game HUD -- */

static uint32_t chip_bg(const Theme *t) {
	return (t->surface & 0x00FFFFFFu) | HUD_ALPHA;
}

static int chip_text_y(int y) {
	return y + (HUD_CHIP_H - BODY_TEXT_H) / 2;
}

static void draw_toast(Platform *p, const Theme *t, const char *text) {
	int w = text_w(text, SCALE_BODY) + 2 * HUD_PAD + 20;

	fill_card(p, HUD_MARGIN, HUD_MARGIN, w, HUD_CHIP_H, chip_bg(t));
	platform_fill_rect(p, HUD_MARGIN + HUD_PAD, HUD_MARGIN + (HUD_CHIP_H - HUD_DOT) / 2, HUD_DOT,
					   HUD_DOT, t->accent);
	platform_draw_text(p, HUD_MARGIN + HUD_PAD + 20, chip_text_y(HUD_MARGIN), SCALE_BODY, t->text,
					   text);
}

static void draw_speed(Platform *p, const Theme *t, int speed) {
	char tag[16];

	snprintf(tag, sizeof tag, "%dx", speed);

	int w = text_w(tag, SCALE_BODY) + 2 * HUD_PAD;
	int x = UI_VIEW_W - HUD_MARGIN - w;

	fill_card(p, x, HUD_MARGIN, w, HUD_CHIP_H, t->accent);
	platform_draw_text(p, x + HUD_PAD, chip_text_y(HUD_MARGIN), SCALE_BODY, t->on_accent, tag);
}

/* "Paused" chip with a pause glyph (two bars). */
static void draw_paused(Platform *p, const Theme *t) {
	const char *text = "Paused";
	int w = 2 * HUD_PAD + 22 + text_w(text, SCALE_BODY);
	int y = UI_VIEW_H - HUD_MARGIN - HUD_CHIP_H;

	fill_card(p, HUD_MARGIN, y, w, HUD_CHIP_H, chip_bg(t));
	platform_fill_rect(p, HUD_MARGIN + HUD_PAD, y + 10, 4, 12, t->accent);
	platform_fill_rect(p, HUD_MARGIN + HUD_PAD + 8, y + 10, 4, 12, t->accent);
	platform_draw_text(p, HUD_MARGIN + HUD_PAD + 22, chip_text_y(y), SCALE_BODY, t->text, text);
}

/* Program counter chip; shows "m<N>" (M-cycles into the instruction) when stepping mid-instruction.
 */
static void draw_pc(Platform *p, const Theme *t, const Hud *hud) {
	char hex[8];
	char step[16] = "";

	snprintf(hex, sizeof hex, "%04X", hud->pc & 0xFFFFu);
	if (hud->mid_instruction) {
		snprintf(step, sizeof step, "m%u", hud->mcycles);
	}

	int label_w = text_w("PC ", SCALE_BODY);
	int hex_w = text_w(hex, SCALE_BODY);
	int step_w = step[0] ? 16 + text_w(step, SCALE_BODY) : 0;
	int w = 2 * HUD_PAD + label_w + hex_w + step_w;
	int x = UI_VIEW_W - HUD_MARGIN - w;
	int y = UI_VIEW_H - HUD_MARGIN - HUD_CHIP_H;
	int ty = chip_text_y(y);

	fill_card(p, x, y, w, HUD_CHIP_H, chip_bg(t));
	platform_draw_text(p, x + HUD_PAD, ty, SCALE_BODY, t->dim, "PC ");
	platform_draw_text(p, x + HUD_PAD + label_w, ty, SCALE_BODY, t->text, hex);
	if (step[0]) {
		platform_draw_text(p, x + HUD_PAD + label_w + hex_w + 16, ty, SCALE_BODY, t->accent, step);
	}
}

void ui_draw_hud(const Ui *ui, Platform *p, const Hud *hud) {
	const Theme *t = theme_get(ui->settings->theme, ui->system_dark);

	if (hud->toast && hud->toast[0]) {
		draw_toast(p, t, hud->toast);
	}
	if (hud->speed > 1) {
		draw_speed(p, t, hud->speed);
	}
	if (hud->paused) {
		draw_paused(p, t);
		draw_pc(p, t, hud);
	}
}

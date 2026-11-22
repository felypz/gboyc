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
#define PAD_ROW_H		32
#define PAD_ROW_PITCH	36
#define PAD_COL_X		(MARGIN_X + 128)
#define PAD_CELL_W		168
#define PAD_CELL_GAP	8
#define SHORTCUT_CELL_X (CONTENT_RIGHT - ROW_PAD - PAD_CELL_W)

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

/* Controls screen: one row per button, then "reset" and "back". */
#define PAD_ROWS  (JOY_BTN_COUNT + 2)
#define PAD_RESET JOY_BTN_COUNT
#define PAD_BACK  (JOY_BTN_COUNT + 1)

/* Shortcuts screen: one row per action, then "reset" and "back". */
#define SHORTCUT_ROWS	   (SC_COUNT + 2)
#define SHORTCUT_RESET_ROW SC_COUNT
#define SHORTCUT_BACK_ROW  (SC_COUNT + 1)

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
	"Continue", "Reset", "Game controls", "Emulator shortcuts", "Load ROM", "Main menu"};
static const char *const PAUSE_VALUES[PAUSE_COUNT] = {NULL, NULL, ">", ">", NULL, NULL};
static const char *const CRASH_LABELS[CR_COUNT] = {"Reset", "Load state", "Load ROM", "Main menu",
												   "Dismiss"};

/* Row order on the controls screen. */
static const JoyButton PAD_ORDER[JOY_BTN_COUNT] = {
	JOY_BTN_UP, JOY_BTN_DOWN, JOY_BTN_LEFT,	  JOY_BTN_RIGHT,
	JOY_BTN_A,	JOY_BTN_B,	  JOY_BTN_SELECT, JOY_BTN_START,
};

static const Hint HINTS_MENU[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}};
static const Hint HINTS_PAUSE[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}, {"ESC", "Resume"}};
static const Hint HINTS_SETTINGS[] = {
	{"UP/DOWN", "Select"}, {"L/R", "Adjust"}, {"ENTER", "Open"}, {"ESC", "Back"}};
static const Hint HINTS_CONTROLS[] = {
	{"UP/DOWN", "Select"}, {"L/R", "Slot"}, {"ENTER", "Rebind"}, {"DEL", "Clear"}, {"ESC", "Back"}};
static const Hint HINTS_SHORTCUTS[] = {
	{"UP/DOWN", "Select"}, {"ENTER", "Rebind"}, {"DEL", "Clear"}, {"ESC", "Back"}};
static const Hint HINTS_CAPTURE[] = {{"KEY", "Bind"}, {"ESC", "Cancel"}};
static const Hint HINTS_CRASH[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}};

static ListGeom geom(UiScreen screen) {
	switch (screen) {
	case UI_SCREEN_MENU:
		return (ListGeom){HERO_LIST_Y, ROW_PITCH, ROW_H, MENU_COUNT};
	case UI_SCREEN_SETTINGS:
		return (ListGeom){LIST_Y, ROW_PITCH, ROW_H, SETTINGS_COUNT};
	case UI_SCREEN_CONTROLS:
		return (ListGeom){LIST_Y, PAD_ROW_PITCH, PAD_ROW_H, PAD_ROWS};
	case UI_SCREEN_SHORTCUTS:
		return (ListGeom){LIST_Y, PAD_ROW_PITCH, PAD_ROW_H, SHORTCUT_ROWS};
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
	memset(ui, 0, sizeof *ui);
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
	go_child(ui, UI_SCREEN_CONTROLS);
	ui->slot = 0;
	ui->capturing = false;
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

static void adjust_setting(Ui *ui, int dir) {
	if (ui->row == SET_BOOT_ROM_ENABLED) {
		toggle_boot_rom(ui);
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

/* -- Key capture (controls and shortcuts screens) -- */

static void stop_capture(Ui *ui, Platform *p, const char *status) {
	ui->capturing = false;
	platform_capture_keys(p, false);
	set_status(ui, status, false);
}

static void start_capture(Ui *ui, Platform *p, const char *name) {
	char text[48];

	ui->capturing = true;
	platform_capture_keys(p, true);
	snprintf(text, sizeof text, "Press a key for %s...", name);
	set_status(ui, text, false);
}

static void bind_captured_key(Ui *ui, Platform *p, const PlatformEvent *ev) {
	bool is_pad = ui->screen == UI_SCREEN_CONTROLS;
	BindKind kind = is_pad ? BIND_PAD : BIND_SHORTCUT;
	int index = is_pad ? (int)PAD_ORDER[ui->row] : ui->row;
	char msg[48];

	if (ev->key == KEY_NONE || !platform_key_valid(ev->key)) {
		return;
	}
	if (bindings_conflict(&ui->settings->bindings, kind, index, ui->slot, ev->key, msg,
						  sizeof msg)) {
		set_status(ui, msg, true);
		return;
	}

	bindings_set(&ui->settings->bindings, kind, index, ui->slot, ev->key);
	stop_capture(ui, p, "Key bound");
	save_settings(ui);
}

/* While waiting for a key, only the captured key or ESC matter. Returns true if consumed. */
static bool capture_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (!ui->capturing) {
		return false;
	}
	if (ev->type == PLATFORM_EV_KEY_CAPTURED) {
		bind_captured_key(ui, p, ev);
	} else if (ev->type == PLATFORM_EV_BACK) {
		stop_capture(ui, p, "");
	}
	return true;
}

static void pad_confirm(Ui *ui, Platform *p) {
	if (ui->row == PAD_BACK) {
		go(ui, ui->parent);
	} else if (ui->row == PAD_RESET) {
		platform_default_pad(&ui->settings->bindings);
		set_status(ui, "Defaults restored", false);
		save_settings(ui);
	} else {
		start_capture(ui, p, bindings_pad_label(PAD_ORDER[ui->row]));
	}
}

static void pad_clear(Ui *ui) {
	if (ui->row >= JOY_BTN_COUNT) {
		return;
	}

	bindings_set(&ui->settings->bindings, BIND_PAD, PAD_ORDER[ui->row], ui->slot, KEY_NONE);
	set_status(ui, "Key cleared", false);
	save_settings(ui);
}

static void shortcut_confirm(Ui *ui, Platform *p) {
	if (ui->row == SHORTCUT_BACK_ROW) {
		go(ui, ui->parent);
	} else if (ui->row == SHORTCUT_RESET_ROW) {
		platform_default_shortcuts(&ui->settings->bindings);
		set_status(ui, "Defaults restored", false);
		save_settings(ui);
	} else {
		start_capture(ui, p, bindings_shortcut_label((ShortcutAction)ui->row));
	}
}

static void shortcut_clear(Ui *ui) {
	if (ui->row >= SC_COUNT) {
		return;
	}

	bindings_set(&ui->settings->bindings, BIND_SHORTCUT, ui->row, 0, KEY_NONE);
	set_status(ui, "Key cleared", false);
	save_settings(ui);
}

/* -- Per-screen events -- */

static void controls_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (capture_event(ui, p, ev) || list_navigate(ui, ev->type)) {
		return;
	}

	switch (ev->type) {
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
		if (ui->row < JOY_BTN_COUNT) {
			ui->slot ^= 1;
		}
		break;
	case PLATFORM_EV_CONFIRM:
		pad_confirm(ui, p);
		break;
	case PLATFORM_EV_CLEAR:
		pad_clear(ui);
		break;
	case PLATFORM_EV_BACK:
		go(ui, ui->parent);
		break;
	default:
		break;
	}
}

static void shortcuts_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (capture_event(ui, p, ev) || list_navigate(ui, ev->type)) {
		return;
	}

	switch (ev->type) {
	case PLATFORM_EV_CONFIRM:
		shortcut_confirm(ui, p);
		break;
	case PLATFORM_EV_CLEAR:
		shortcut_clear(ui);
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
	case UI_SCREEN_CONTROLS:
		controls_event(ui, p, ev);
		break;
	case UI_SCREEN_SHORTCUTS:
		shortcuts_event(ui, p, ev);
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
 * *on_cell tells whether it also hit something clickable (on the controls screen only
 * the key cells are, and the hit key slot is selected).
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

	if (ui->screen == UI_SCREEN_CONTROLS && idx < JOY_BTN_COUNT) {
		*on_cell = false;
		for (int s = 0; s < BIND_KEY_SLOTS; s++) {
			int x = PAD_COL_X + s * (PAD_CELL_W + PAD_CELL_GAP);

			if (mx >= x && mx < x + PAD_CELL_W) {
				ui->slot = s;
				*on_cell = true;
			}
		}
	}
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
		case SET_CONTROLS:
			label = "Game controls";
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

static int pad_cell_x(int slot) {
	return PAD_COL_X + slot * (PAD_CELL_W + PAD_CELL_GAP);
}

static void draw_pad_row(const Ui *ui, Platform *p, const Theme *t, int row) {
	ListGeom g = geom(UI_SCREEN_CONTROLS);
	int y = g.y0 + row * g.pitch;
	bool selected = row == ui->row;

	if (row >= JOY_BTN_COUNT) {
		draw_row(p, t, y, g.h, selected, row == PAD_RESET ? "Reset to default" : "Back", NULL,
				 false);
		return;
	}

	JoyButton button = PAD_ORDER[row];

	draw_label_row(p, t, y, g.h, selected, bindings_pad_label(button));

	for (int s = 0; s < BIND_KEY_SLOTS; s++) {
		char name[16];
		bool active = selected && s == ui->slot;

		key_label(bindings_get(&ui->settings->bindings, BIND_PAD, (int)button, s), name,
				  sizeof name);
		if (ui->capturing && active) {
			snprintf(name, sizeof name, "...");
		}
		draw_key_cell(p, t, pad_cell_x(s), y, selected, active, name);
	}
}

static void draw_controls(const Ui *ui, Platform *p, const Theme *t) {
	draw_header(p, t, "Controls");
	platform_draw_text(p, MARGIN_X, SUBHEADER_Y, SCALE_CAPTION, t->dim, "Game Boy buttons");

	for (int s = 0; s < BIND_KEY_SLOTS; s++) {
		char title[8];

		snprintf(title, sizeof title, "KEY %d", s + 1);
		text_center(p, pad_cell_x(s) + PAD_CELL_W / 2, SUBHEADER_Y, SCALE_CAPTION, t->dim, title);
	}
	for (int row = 0; row < PAD_ROWS; row++) {
		draw_pad_row(ui, p, t, row);
	}

	draw_status(ui, p, t);
	if (ui->capturing) {
		draw_hints(p, t, HINTS_CAPTURE, COUNT(HINTS_CAPTURE));
	} else {
		draw_hints(p, t, HINTS_CONTROLS, COUNT(HINTS_CONTROLS));
	}
}

static void draw_shortcut_row(const Ui *ui, Platform *p, const Theme *t, int row) {
	ListGeom g = geom(UI_SCREEN_SHORTCUTS);
	int y = g.y0 + row * g.pitch;
	bool selected = row == ui->row;

	if (row >= SC_COUNT) {
		draw_row(p, t, y, g.h, selected, row == SHORTCUT_RESET_ROW ? "Reset to default" : "Back",
				 NULL, false);
		return;
	}

	draw_label_row(p, t, y, g.h, selected, bindings_shortcut_label((ShortcutAction)row));

	char name[16];

	key_label(bindings_get(&ui->settings->bindings, BIND_SHORTCUT, row, 0), name, sizeof name);
	if (ui->capturing && selected) {
		snprintf(name, sizeof name, "...");
	}
	draw_key_cell(p, t, SHORTCUT_CELL_X, y, selected, selected, name);
}

static void draw_shortcuts(const Ui *ui, Platform *p, const Theme *t) {
	draw_header(p, t, "Shortcuts");
	platform_draw_text(p, MARGIN_X, SUBHEADER_Y, SCALE_CAPTION, t->dim,
					   "Emulator actions. State slots: keys 1-9");

	for (int row = 0; row < SHORTCUT_ROWS; row++) {
		draw_shortcut_row(ui, p, t, row);
	}

	draw_status(ui, p, t);
	if (ui->capturing) {
		draw_hints(p, t, HINTS_CAPTURE, COUNT(HINTS_CAPTURE));
	} else {
		draw_hints(p, t, HINTS_SHORTCUTS, COUNT(HINTS_SHORTCUTS));
	}
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
	case UI_SCREEN_CONTROLS:
		draw_controls(ui, p, t);
		break;
	case UI_SCREEN_SHORTCUTS:
		draw_shortcuts(ui, p, t);
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

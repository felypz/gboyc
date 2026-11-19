#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "bindings.h"
#include "boot.h"
#include "palette.h"
#include "theme.h"

#define GLYPH	 8
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define SCALE_HERO	  5
#define SCALE_TITLE	  3
#define SCALE_BODY	  2
#define SCALE_CAPTION 1

#define MARGIN_X		64
#define CONTENT_W		(UI_VIEW_W - 2 * MARGIN_X)
#define HEADER_LINE_Y	72
#define LIST_Y			112
#define HERO_LIST_Y		250
#define ROW_H			40
#define ROW_PITCH		48
#define ROW_PAD			16
#define PAD_ROW_H		32
#define PAD_ROW_PITCH	36
#define PAD_COL_X		(MARGIN_X + 128)
#define PAD_CELL_W		168
#define PAD_CELL_GAP	8
#define SHORTCUT_CELL_X (MARGIN_X + CONTENT_W - ROW_PAD - PAD_CELL_W)
#define STATUS_Y		480
#define STATUS_H		32
#define FOOTER_LINE_Y	524
#define HINT_Y			542
#define OVERLAY_ALPHA	0xB4000000u

#define HUD_MARGIN 20
#define HUD_CHIP_H 32
#define HUD_PAD	   14
#define HUD_ALPHA  0xE6000000u

enum {
	MENU_LOAD_ROM,
	MENU_SETTINGS,
	MENU_QUIT,
	MENU_COUNT,
};

enum {
	SET_BOOT,
	SET_BOOT_FILE,
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

#define PAD_ROWS  (JOY_BUTTONS + 2)
#define PAD_RESET JOY_BUTTONS
#define PAD_BACK  (JOY_BUTTONS + 1)

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

static const int PAD_ORDER[JOY_BUTTONS] = {2, 3, 1, 0, 4, 5, 6, 7};

static const Hint HINTS_MENU[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}};
static const Hint HINTS_PAUSE[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}, {"ESC", "Resume"}};
static const Hint HINTS_SETTINGS[] = {
	{"UP/DOWN", "Select"}, {"L/R", "Adjust"}, {"ENTER", "Open"}, {"ESC", "Back"}};
static const Hint HINTS_CONTROLS[] = {
	{"UP/DOWN", "Select"}, {"L/R", "Slot"}, {"ENTER", "Rebind"}, {"DEL", "Clear"}, {"ESC", "Back"}};
static const Hint HINTS_SHORTCUTS[] = {
	{"UP/DOWN", "Select"}, {"ENTER", "Rebind"}, {"DEL", "Clear"}, {"ESC", "Back"}};
static const Hint HINTS_CAPTURE[] = {{"KEY", "Bind"}, {"ESC", "Cancel"}};

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
	default:
		return (ListGeom){0, 1, 0, 0};
	}
}

static void set_status(Ui *ui, const char *text, bool warn) {
	snprintf(ui->status, sizeof ui->status, "%s", text);
	ui->status_warn = warn;
}

static void save(Ui *ui) {
	if (!settings_save(ui->settings)) {
		set_status(ui, "Could not save settings", true);
	}
}

static void go(Ui *ui, UiScreen screen) {
	ui->sel[ui->screen] = ui->row;
	ui->screen = screen;
	ui->row = ui->sel[screen];
	ui->status[0] = '\0';
}

static void move(Ui *ui, int dir) {
	int n = geom(ui->screen).count;

	if (n > 0) {
		ui->row = (ui->row + n + dir) % n;
	}
}

static void key_label(int scancode, char *out, size_t size) {
	const char *name = scancode > 0 ? platform_key_name(scancode) : NULL;

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

static void fit(char *out, size_t size, const char *in, size_t max) {
	if (strlen(in) <= max) {
		snprintf(out, size, "%s", in);
	} else {
		snprintf(out, size, "%.*s...", (int)max - 3, in);
	}
}

void ui_init(Ui *ui, Settings *settings) {
	memset(ui, 0, sizeof *ui);
	ui->settings = settings;
	ui->system_dark = platform_system_dark();
}

void ui_open_pause(Ui *ui) {
	Settings *settings = ui->settings;

	memset(ui, 0, sizeof *ui);
	ui->settings = settings;
	ui->system_dark = platform_system_dark();
	ui->screen = UI_SCREEN_PAUSE;
	ui->in_game = true;
}

void ui_notify(Ui *ui, const char *text, bool warn) {
	set_status(ui, text, warn);
}

static void open_settings(Ui *ui) {
	go(ui, UI_SCREEN_SETTINGS);
	ui->boot_ok = boot_check(ui->settings->boot_file);
}

static void open_controls(Ui *ui) {
	ui->parent = ui->screen;
	go(ui, UI_SCREEN_CONTROLS);
	ui->slot = 0;
	ui->capturing = false;
}

static void open_shortcuts(Ui *ui) {
	ui->parent = ui->screen;
	go(ui, UI_SCREEN_SHORTCUTS);
	ui->capturing = false;
}

static void open_rom_dialog(Ui *ui, Platform *p) {
	ui->dialog_open = true;
	ui->dialog_boot = false;
	platform_open_file_dialog(p, false);
}

static void toggle_boot(Ui *ui) {
	if (!ui->boot_ok) {
		set_status(ui, "No boot ROM found", true);
		return;
	}

	ui->settings->boot_enabled = !ui->settings->boot_enabled;
	ui->status[0] = '\0';
	save(ui);
}

static void cycle_theme(Ui *ui, int dir) {
	ui->settings->theme = (ThemeMode)(((int)ui->settings->theme + THEME_COUNT + dir) % THEME_COUNT);
	save(ui);
}

static void adjust(Ui *ui, int dir) {
	if (ui->row == SET_BOOT) {
		toggle_boot(ui);
	} else if (ui->row == SET_THEME) {
		cycle_theme(ui, dir);
	}
}

static void import_boot(Ui *ui, const char *path) {
	char name[SETTINGS_NAME_MAX];
	BootResult result = boot_import(path, name, sizeof name);

	if (result != BOOT_OK) {
		set_status(ui, boot_result_text(result), true);
		return;
	}

	snprintf(ui->settings->boot_file, sizeof ui->settings->boot_file, "%s", name);
	ui->settings->boot_enabled = true;
	ui->boot_ok = true;
	set_status(ui, "Boot ROM imported", false);
	save(ui);
}

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

static void capture_key(Ui *ui, Platform *p, const PlatformEvent *ev) {
	bool pad = ui->screen == UI_SCREEN_CONTROLS;
	BindKind kind = pad ? BIND_PAD : BIND_SHORTCUT;
	int index = pad ? PAD_ORDER[ui->row] : ui->row;
	char msg[48];

	if (ev->key <= 0 || !platform_key_valid(ev->key)) {
		return;
	}
	if (bindings_conflict(&ui->settings->bindings, kind, index, ui->slot, ev->key, msg,
						  sizeof msg)) {
		set_status(ui, msg, true);
		return;
	}

	bindings_set(&ui->settings->bindings, kind, index, ui->slot, ev->key);
	stop_capture(ui, p, "Key bound");
	save(ui);
}

static void pad_confirm(Ui *ui, Platform *p) {
	if (ui->row == PAD_BACK) {
		go(ui, ui->parent);
	} else if (ui->row == PAD_RESET) {
		platform_default_pad(&ui->settings->bindings);
		set_status(ui, "Defaults restored", false);
		save(ui);
	} else {
		start_capture(ui, p, bindings_pad_label(PAD_ORDER[ui->row]));
	}
}

static void pad_clear(Ui *ui) {
	if (ui->row >= JOY_BUTTONS) {
		return;
	}

	bindings_set(&ui->settings->bindings, BIND_PAD, PAD_ORDER[ui->row], ui->slot, 0);
	set_status(ui, "Key cleared", false);
	save(ui);
}

static void shortcut_confirm(Ui *ui, Platform *p) {
	if (ui->row == SHORTCUT_BACK_ROW) {
		go(ui, ui->parent);
	} else if (ui->row == SHORTCUT_RESET_ROW) {
		platform_default_shortcuts(&ui->settings->bindings);
		set_status(ui, "Defaults restored", false);
		save(ui);
	} else {
		start_capture(ui, p, bindings_shortcut_label((ShortcutAction)ui->row));
	}
}

static void shortcut_clear(Ui *ui) {
	if (ui->row >= SC_COUNT) {
		return;
	}

	bindings_set(&ui->settings->bindings, BIND_SHORTCUT, ui->row, 0, 0);
	set_status(ui, "Key cleared", false);
	save(ui);
}

static void controls_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	if (ui->capturing) {
		if (ev->type == PLATFORM_EV_KEY_CAPTURED) {
			capture_key(ui, p, ev);
		} else if (ev->type == PLATFORM_EV_BACK) {
			stop_capture(ui, p, "");
		}
		return;
	}

	switch (ev->type) {
	case PLATFORM_EV_UP:
		move(ui, -1);
		break;
	case PLATFORM_EV_DOWN:
		move(ui, 1);
		break;
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
		if (ui->row < JOY_BUTTONS) {
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
	if (ui->capturing) {
		if (ev->type == PLATFORM_EV_KEY_CAPTURED) {
			capture_key(ui, p, ev);
		} else if (ev->type == PLATFORM_EV_BACK) {
			stop_capture(ui, p, "");
		}
		return;
	}

	switch (ev->type) {
	case PLATFORM_EV_UP:
		move(ui, -1);
		break;
	case PLATFORM_EV_DOWN:
		move(ui, 1);
		break;
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
	switch (ev->type) {
	case PLATFORM_EV_UP:
		move(ui, -1);
		break;
	case PLATFORM_EV_DOWN:
		move(ui, 1);
		break;
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
		adjust(ui, ev->type == PLATFORM_EV_RIGHT ? 1 : -1);
		break;
	case PLATFORM_EV_CONFIRM:
		if (ui->row == SET_BOOT) {
			toggle_boot(ui);
		} else if (ui->row == SET_BOOT_FILE) {
			ui->dialog_open = true;
			ui->dialog_boot = true;
			platform_open_file_dialog(p, true);
		} else if (ui->row == SET_CONTROLS) {
			open_controls(ui);
		} else if (ui->row == SET_SHORTCUTS) {
			open_shortcuts(ui);
		} else if (ui->row == SET_THEME) {
			cycle_theme(ui, 1);
		} else {
			go(ui, UI_SCREEN_MENU);
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
	switch (ev->type) {
	case PLATFORM_EV_UP:
		move(ui, -1);
		break;
	case PLATFORM_EV_DOWN:
		move(ui, 1);
		break;
	case PLATFORM_EV_CONFIRM:
		if (ui->row == MENU_QUIT) {
			return UI_ACTION_QUIT;
		}
		if (ui->row == MENU_SETTINGS) {
			open_settings(ui);
		} else {
			open_rom_dialog(ui, p);
		}
		break;
	default:
		break;
	}
	return UI_ACTION_NONE;
}

static UiAction pause_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_UP:
		move(ui, -1);
		break;
	case PLATFORM_EV_DOWN:
		move(ui, 1);
		break;
	case PLATFORM_EV_BACK:
		return UI_ACTION_RESUME;
	case PLATFORM_EV_CONFIRM:
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
			open_rom_dialog(ui, p);
			break;
		default:
			return UI_ACTION_MAIN_MENU;
		}
		break;
	default:
		break;
	}
	return UI_ACTION_NONE;
}

static UiAction dispatch(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ui->screen) {
	case UI_SCREEN_MENU:
		return menu_event(ui, p, ev);
	case UI_SCREEN_PAUSE:
		return pause_event(ui, p, ev);
	case UI_SCREEN_DROP:
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
	default:
		break;
	}
	return UI_ACTION_NONE;
}

static UiAction file_chosen(Ui *ui, const PlatformEvent *ev) {
	if (ui->dialog_boot) {
		ui->dialog_open = false;
		ui->dialog_boot = false;
		import_boot(ui, ev->path);
		return UI_ACTION_NONE;
	}
	if (ui->screen != UI_SCREEN_MENU && ui->screen != UI_SCREEN_DROP &&
		ui->screen != UI_SCREEN_PAUSE) {
		return UI_ACTION_NONE;
	}

	ui->dialog_open = false;
	snprintf(ui->rom_path, sizeof ui->rom_path, "%s", ev->path);
	return UI_ACTION_LOAD_ROM;
}

static void dialog_failed(Ui *ui) {
	bool boot = ui->dialog_boot;

	ui->dialog_open = false;
	ui->dialog_boot = false;
	if (boot) {
		set_status(ui, "File dialog unavailable", true);
		return;
	}

	ui->parent = ui->screen;
	go(ui, UI_SCREEN_DROP);
}

static bool pick_row(Ui *ui, int mx, int my, bool *on_cell) {
	ListGeom g = geom(ui->screen);
	int rel = my - g.y0;

	if (g.count == 0 || mx < MARGIN_X || mx >= MARGIN_X + CONTENT_W || rel < 0) {
		return false;
	}

	int idx = rel / g.pitch;

	if (idx >= g.count || rel % g.pitch >= g.h) {
		return false;
	}

	ui->row = idx;
	*on_cell = true;

	if (ui->screen == UI_SCREEN_CONTROLS && idx < JOY_BUTTONS) {
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
		ui->dialog_open = false;
		ui->dialog_boot = false;
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

static int text_w(const char *text, int scale) {
	return (int)strlen(text) * GLYPH * scale;
}

static void text_left(Platform *p, int x, int y, int scale, uint32_t color, const char *text) {
	platform_draw_text(p, x, y, scale, color, text);
}

static void text_right(Platform *p, int right, int y, int scale, uint32_t color, const char *text) {
	platform_draw_text(p, right - text_w(text, scale), y, scale, color, text);
}

static void text_center(Platform *p, int cx, int y, int scale, uint32_t color, const char *text) {
	platform_draw_text(p, cx - text_w(text, scale) / 2, y, scale, color, text);
}

static void fill_card(Platform *p, int x, int y, int w, int h, uint32_t color) {
	platform_fill_rect(p, x + 3, y, w - 6, h, color);
	platform_fill_rect(p, x + 1, y + 1, w - 2, h - 2, color);
	platform_fill_rect(p, x, y + 3, w, h - 6, color);
}

static void draw_mark(Platform *p, int x, int y, int cell) {
	platform_fill_rect(p, x, y, cell, cell, DMG_COLOR_0);
	platform_fill_rect(p, x + cell + 2, y, cell, cell, DMG_COLOR_1);
	platform_fill_rect(p, x, y + cell + 2, cell, cell, DMG_COLOR_2);
	platform_fill_rect(p, x + cell + 2, y + cell + 2, cell, cell, DMG_COLOR_3);
}

static void draw_header(Platform *p, const Theme *t, const char *title) {
	draw_mark(p, MARGIN_X, 28, 12);
	text_left(p, MARGIN_X + 42, 30, SCALE_TITLE, t->text, title);
	text_right(p, MARGIN_X + CONTENT_W, 34, SCALE_BODY, t->dim, "GBOYC");
	platform_fill_rect(p, MARGIN_X, HEADER_LINE_Y, CONTENT_W, 2, t->line);
}

static void draw_hints(Platform *p, const Theme *t, const Hint *hints, int count) {
	int x = MARGIN_X;

	platform_fill_rect(p, MARGIN_X, FOOTER_LINE_Y, CONTENT_W, 2, t->line);
	for (int i = 0; i < count; i++) {
		int key_w = text_w(hints[i].key, SCALE_CAPTION) + 12;

		fill_card(p, x, HINT_Y - 6, key_w, 20, t->surface);
		text_left(p, x + 6, HINT_Y, SCALE_CAPTION, t->text, hints[i].key);
		x += key_w + 6;
		text_left(p, x, HINT_Y, SCALE_CAPTION, t->dim, hints[i].label);
		x += text_w(hints[i].label, SCALE_CAPTION) + 16;
	}
}

static void draw_status(const Ui *ui, Platform *p, const Theme *t) {
	const char *text = ui->dialog_open ? "Waiting for file dialog..." : ui->status;
	bool warn = !ui->dialog_open && ui->status_warn;

	if (!text[0]) {
		return;
	}

	fill_card(p, MARGIN_X, STATUS_Y, CONTENT_W, STATUS_H, t->surface);
	platform_fill_rect(p, MARGIN_X + 12, STATUS_Y + (STATUS_H - 8) / 2, 8, 8,
					   warn ? t->warn : t->accent);
	text_left(p, MARGIN_X + 28, STATUS_Y + (STATUS_H - GLYPH * SCALE_BODY) / 2, SCALE_BODY, t->text,
			  text);
}

static void draw_row(Platform *p, const Theme *t, int y, int h, bool selected, const char *label,
					 const char *value, bool adjustable) {
	int ty = y + (h - GLYPH * SCALE_BODY) / 2;

	fill_card(p, MARGIN_X, y, CONTENT_W, h, selected ? t->accent : t->surface);
	text_left(p, MARGIN_X + ROW_PAD, ty, SCALE_BODY, selected ? t->on_accent : t->text, label);

	if (value) {
		char buf[48];

		if (selected && adjustable) {
			snprintf(buf, sizeof buf, "< %s >", value);
		} else {
			snprintf(buf, sizeof buf, "%s", value);
		}
		text_right(p, MARGIN_X + CONTENT_W - ROW_PAD, ty, SCALE_BODY,
				   selected ? t->on_accent : t->dim, buf);
	}
}

static void draw_menu(const Ui *ui, Platform *p, const Theme *t) {
	int cx = UI_VIEW_W / 2;
	ListGeom g = geom(UI_SCREEN_MENU);

	draw_mark(p, cx - 25, 56, 24);
	text_center(p, cx, 124, SCALE_HERO, t->text, "GBOYC");
	text_center(p, cx, 180, SCALE_BODY, t->dim, "Game Boy emulator");

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

static void draw_dashed_rect(Platform *p, int x, int y, int w, int h, uint32_t color) {
	for (int i = 0; i < w; i += 16) {
		int len = (w - i < 8) ? w - i : 8;

		platform_fill_rect(p, x + i, y, len, 2, color);
		platform_fill_rect(p, x + i, y + h - 2, len, 2, color);
	}
	for (int i = 0; i < h; i += 16) {
		int len = (h - i < 8) ? h - i : 8;

		platform_fill_rect(p, x, y + i, 2, len, color);
		platform_fill_rect(p, x + w - 2, y + i, 2, len, color);
	}
}

static void draw_drop(Platform *p, const Theme *t) {
	int cx = UI_VIEW_W / 2;

	draw_header(p, t, "Load ROM");
	draw_dashed_rect(p, 96, 176, 448, 176, t->line);
	text_center(p, cx, 238, SCALE_BODY, t->text, "Drop the ROM file here");
	text_center(p, cx, 276, SCALE_CAPTION, t->dim, "Press ESC to go back");
}

static void draw_settings(const Ui *ui, Platform *p, const Theme *t) {
	char file[24];
	ListGeom g = geom(UI_SCREEN_SETTINGS);

	draw_header(p, t, "Settings");

	for (int i = 0; i < SETTINGS_COUNT; i++) {
		const char *label = "Back";
		const char *value = NULL;
		bool adjustable = false;

		switch (i) {
		case SET_BOOT:
			label = "Boot animation";
			value = !ui->boot_ok ? "Unavailable" : (ui->settings->boot_enabled ? "ON" : "OFF");
			adjustable = ui->boot_ok;
			break;
		case SET_BOOT_FILE:
			label = "Boot ROM";
			fit(file, sizeof file, ui->boot_ok ? ui->settings->boot_file : "none", 14);
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
			value = theme_mode_name(ui->settings->theme);
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
	text_center(p, x + PAD_CELL_W / 2, y + (PAD_ROW_H - GLYPH * SCALE_BODY) / 2, SCALE_BODY, fg,
				name);
}

static void draw_pad_row(const Ui *ui, Platform *p, const Theme *t, int row) {
	ListGeom g = geom(UI_SCREEN_CONTROLS);
	int y = g.y0 + row * g.pitch;
	bool selected = row == ui->row;

	if (row >= JOY_BUTTONS) {
		draw_row(p, t, y, g.h, selected, row == PAD_RESET ? "Reset to default" : "Back", NULL,
				 false);
		return;
	}

	int button = PAD_ORDER[row];

	fill_card(p, MARGIN_X, y, CONTENT_W, g.h, selected ? t->accent : t->surface);
	text_left(p, MARGIN_X + ROW_PAD, y + (g.h - GLYPH * SCALE_BODY) / 2, SCALE_BODY,
			  selected ? t->on_accent : t->text, bindings_pad_label(button));

	for (int s = 0; s < BIND_KEY_SLOTS; s++) {
		char name[16];
		bool active = selected && s == ui->slot;

		key_label(bindings_get(&ui->settings->bindings, BIND_PAD, button, s), name, sizeof name);
		if (ui->capturing && active) {
			snprintf(name, sizeof name, "...");
		}
		draw_key_cell(p, t, PAD_COL_X + s * (PAD_CELL_W + PAD_CELL_GAP), y, selected, active, name);
	}
}

static void draw_controls(const Ui *ui, Platform *p, const Theme *t) {
	draw_header(p, t, "Controls");
	text_left(p, MARGIN_X, 92, SCALE_CAPTION, t->dim, "Game Boy buttons");

	for (int s = 0; s < BIND_KEY_SLOTS; s++) {
		char title[8];

		snprintf(title, sizeof title, "KEY %d", s + 1);
		text_center(p, PAD_COL_X + s * (PAD_CELL_W + PAD_CELL_GAP) + PAD_CELL_W / 2, 92,
					SCALE_CAPTION, t->dim, title);
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

	fill_card(p, MARGIN_X, y, CONTENT_W, g.h, selected ? t->accent : t->surface);
	text_left(p, MARGIN_X + ROW_PAD, y + (g.h - GLYPH * SCALE_BODY) / 2, SCALE_BODY,
			  selected ? t->on_accent : t->text, bindings_shortcut_label((ShortcutAction)row));

	char name[16];

	key_label(bindings_get(&ui->settings->bindings, BIND_SHORTCUT, row, 0), name, sizeof name);
	if (ui->capturing && selected) {
		snprintf(name, sizeof name, "...");
	}
	draw_key_cell(p, t, SHORTCUT_CELL_X, y, selected, selected, name);
}

static void draw_shortcuts(const Ui *ui, Platform *p, const Theme *t) {
	draw_header(p, t, "Shortcuts");
	text_left(p, MARGIN_X, 92, SCALE_CAPTION, t->dim, "Emulator actions. State slots: keys 1-9");

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
	case UI_SCREEN_DROP:
		draw_drop(p, t);
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
	default:
		break;
	}
}

static uint32_t chip_bg(const Theme *t) {
	return (t->surface & 0x00FFFFFFu) | HUD_ALPHA;
}

static int chip_text_y(int y) {
	return y + (HUD_CHIP_H - GLYPH * SCALE_BODY) / 2;
}

static void draw_toast(Platform *p, const Theme *t, const char *text) {
	int w = text_w(text, SCALE_BODY) + 2 * HUD_PAD + 20;

	fill_card(p, HUD_MARGIN, HUD_MARGIN, w, HUD_CHIP_H, chip_bg(t));
	platform_fill_rect(p, HUD_MARGIN + HUD_PAD, HUD_MARGIN + (HUD_CHIP_H - 8) / 2, 8, 8, t->accent);
	text_left(p, HUD_MARGIN + HUD_PAD + 20, chip_text_y(HUD_MARGIN), SCALE_BODY, t->text, text);
}

static void draw_speed(Platform *p, const Theme *t, int speed) {
	char tag[16];

	snprintf(tag, sizeof tag, "%dx", speed);

	int w = text_w(tag, SCALE_BODY) + 2 * HUD_PAD;
	int x = UI_VIEW_W - HUD_MARGIN - w;

	fill_card(p, x, HUD_MARGIN, w, HUD_CHIP_H, t->accent);
	text_left(p, x + HUD_PAD, chip_text_y(HUD_MARGIN), SCALE_BODY, t->on_accent, tag);
}

static void draw_paused(Platform *p, const Theme *t) {
	const char *text = "Paused";
	int w = 2 * HUD_PAD + 22 + text_w(text, SCALE_BODY);
	int y = UI_VIEW_H - HUD_MARGIN - HUD_CHIP_H;

	fill_card(p, HUD_MARGIN, y, w, HUD_CHIP_H, chip_bg(t));
	platform_fill_rect(p, HUD_MARGIN + HUD_PAD, y + 10, 4, 12, t->accent);
	platform_fill_rect(p, HUD_MARGIN + HUD_PAD + 8, y + 10, 4, 12, t->accent);
	text_left(p, HUD_MARGIN + HUD_PAD + 22, chip_text_y(y), SCALE_BODY, t->text, text);
}

static void draw_pc(Platform *p, const Theme *t, const Hud *hud) {
	char hex[8];
	char step[16] = "";

	snprintf(hex, sizeof hex, "%04X", hud->pc & 0xFFFFu);
	if (hud->mid_instruction) {
		snprintf(step, sizeof step, "c%u", hud->cycles);
	}

	int label_w = text_w("PC ", SCALE_BODY);
	int hex_w = text_w(hex, SCALE_BODY);
	int step_w = step[0] ? 16 + text_w(step, SCALE_BODY) : 0;
	int w = 2 * HUD_PAD + label_w + hex_w + step_w;
	int x = UI_VIEW_W - HUD_MARGIN - w;
	int y = UI_VIEW_H - HUD_MARGIN - HUD_CHIP_H;
	int ty = chip_text_y(y);

	fill_card(p, x, y, w, HUD_CHIP_H, chip_bg(t));
	text_left(p, x + HUD_PAD, ty, SCALE_BODY, t->dim, "PC ");
	text_left(p, x + HUD_PAD + label_w, ty, SCALE_BODY, t->text, hex);
	if (step[0]) {
		text_left(p, x + HUD_PAD + label_w + hex_w + 16, ty, SCALE_BODY, t->accent, step);
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

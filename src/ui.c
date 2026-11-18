#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "boot.h"
#include "palette.h"
#include "theme.h"

#define GLYPH	 8
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define SCALE_HERO	  5
#define SCALE_TITLE	  3
#define SCALE_BODY	  2
#define SCALE_CAPTION 1

#define MARGIN_X	  64
#define CONTENT_W	  (UI_VIEW_W - 2 * MARGIN_X)
#define HEADER_LINE_Y 72
#define LIST_Y		  112
#define HERO_LIST_Y	  250
#define ROW_H		  40
#define ROW_PITCH	  48
#define ROW_PAD		  16
#define PAD_ROW_H	  32
#define PAD_ROW_PITCH 36
#define PAD_COL_X	  (MARGIN_X + 128)
#define PAD_CELL_W	  168
#define PAD_CELL_GAP  8
#define STATUS_Y	  480
#define STATUS_H	  32
#define FOOTER_LINE_Y 524
#define HINT_Y		  542

#define MENU_COUNT	  3
#define MENU_SETTINGS 1
#define MENU_QUIT	  2

#define SETTINGS_COUNT 5
#define SET_BOOT	   0
#define SET_BOOT_FILE  1
#define SET_JOYPAD	   2
#define SET_THEME	   3

#define PAD_ROWS  (JOY_BUTTONS + 2)
#define PAD_RESET JOY_BUTTONS
#define PAD_BACK  (JOY_BUTTONS + 1)

typedef struct {
	const char *key;
	const char *label;
} Hint;

static const char *const MENU_LABELS[MENU_COUNT] = {"Load ROM", "Settings", "Quit"};

static const struct {
	const char *name;
	int index;
} PAD_BUTTONS[JOY_BUTTONS] = {
	{"Up", 2}, {"Down", 3}, {"Left", 1},   {"Right", 0},
	{"A", 4},  {"B", 5},	{"Select", 6}, {"Start", 7},
};

static const Hint HINTS_MENU[] = {{"UP/DOWN", "Select"}, {"ENTER", "Open"}};
static const Hint HINTS_SETTINGS[] = {
	{"UP/DOWN", "Select"}, {"L/R", "Adjust"}, {"ENTER", "Open"}, {"ESC", "Back"}};
static const Hint HINTS_JOYPAD[] = {
	{"UP/DOWN", "Select"}, {"L/R", "Slot"}, {"ENTER", "Rebind"}, {"DEL", "Clear"}, {"ESC", "Back"}};
static const Hint HINTS_CAPTURE[] = {{"KEY", "Bind"}, {"ESC", "Cancel"}};

static void set_status(Ui *ui, const char *text, bool warn) {
	snprintf(ui->status, sizeof ui->status, "%s", text);
	ui->status_warn = warn;
}

static void save(Ui *ui) {
	if (!settings_save(ui->settings)) {
		set_status(ui, "Could not save settings", true);
	}
}

static const char *button_name(int index) {
	for (int i = 0; i < JOY_BUTTONS; i++) {
		if (PAD_BUTTONS[i].index == index) {
			return PAD_BUTTONS[i].name;
		}
	}
	return "?";
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

static void open_settings(Ui *ui) {
	ui->screen = UI_SCREEN_SETTINGS;
	ui->row = 0;
	ui->status[0] = '\0';
	ui->boot_ok = boot_check(ui->settings->boot_file);
}

static void open_joypad(Ui *ui) {
	ui->screen = UI_SCREEN_JOYPAD;
	ui->row = 0;
	ui->slot = 0;
	ui->capturing = false;
	ui->status[0] = '\0';
}

static void leave_settings(Ui *ui) {
	ui->screen = UI_SCREEN_MENU;
	ui->selected = MENU_SETTINGS;
	ui->status[0] = '\0';
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

static void capture_key(Ui *ui, Platform *p, const PlatformEvent *ev) {
	int button = PAD_BUTTONS[ui->row].index;
	JoyKeymap *keymap = &ui->settings->keymap;

	if (ev->key <= 0 || !platform_key_valid(ev->key)) {
		return;
	}
	if (ev->hotkey) {
		snprintf(ui->status, sizeof ui->status, "Used by emulator: %s", ev->hotkey);
		ui->status_warn = true;
		return;
	}

	for (int b = 0; b < JOY_BUTTONS; b++) {
		for (int s = 0; s < JOY_KEY_SLOTS; s++) {
			if (keymap->key[b][s] == ev->key && !(b == button && s == ui->slot)) {
				snprintf(ui->status, sizeof ui->status, "Already bound to %s", button_name(b));
				ui->status_warn = true;
				return;
			}
		}
	}

	keymap->key[button][ui->slot] = ev->key;
	stop_capture(ui, p, "Key bound");
	save(ui);
}

static void pad_confirm(Ui *ui, Platform *p) {
	if (ui->row == PAD_BACK) {
		open_settings(ui);
		return;
	}
	if (ui->row == PAD_RESET) {
		platform_default_keymap(&ui->settings->keymap);
		set_status(ui, "Defaults restored", false);
		save(ui);
		return;
	}

	ui->capturing = true;
	platform_capture_keys(p, true);

	char text[48];

	snprintf(text, sizeof text, "Press a key for %s...", PAD_BUTTONS[ui->row].name);
	set_status(ui, text, false);
}

static void pad_clear(Ui *ui) {
	if (ui->row >= JOY_BUTTONS) {
		return;
	}

	ui->settings->keymap.key[PAD_BUTTONS[ui->row].index][ui->slot] = 0;
	set_status(ui, "Key cleared", false);
	save(ui);
}

static void joypad_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
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
		ui->row = (ui->row + PAD_ROWS - 1) % PAD_ROWS;
		ui->status[0] = '\0';
		break;
	case PLATFORM_EV_DOWN:
		ui->row = (ui->row + 1) % PAD_ROWS;
		ui->status[0] = '\0';
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
		open_settings(ui);
		break;
	default:
		break;
	}
}

static void settings_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_UP:
		ui->row = (ui->row + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
		ui->status[0] = '\0';
		break;
	case PLATFORM_EV_DOWN:
		ui->row = (ui->row + 1) % SETTINGS_COUNT;
		ui->status[0] = '\0';
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
		} else if (ui->row == SET_JOYPAD) {
			open_joypad(ui);
		} else if (ui->row == SET_THEME) {
			cycle_theme(ui, 1);
		} else {
			leave_settings(ui);
		}
		break;
	case PLATFORM_EV_BACK:
		leave_settings(ui);
		break;
	default:
		break;
	}
}

static UiAction menu_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_UP:
		ui->selected = (ui->selected + MENU_COUNT - 1) % MENU_COUNT;
		break;
	case PLATFORM_EV_DOWN:
		ui->selected = (ui->selected + 1) % MENU_COUNT;
		break;
	case PLATFORM_EV_CONFIRM:
		if (ui->selected == MENU_QUIT) {
			return UI_ACTION_QUIT;
		}
		if (ui->selected == MENU_SETTINGS) {
			open_settings(ui);
			break;
		}
		ui->dialog_open = true;
		ui->dialog_boot = false;
		platform_open_file_dialog(p, false);
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
	if (ui->screen != UI_SCREEN_MENU && ui->screen != UI_SCREEN_DROP) {
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
	} else {
		ui->screen = UI_SCREEN_DROP;
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

	if (ui->dialog_open) {
		return UI_ACTION_NONE;
	}

	switch (ui->screen) {
	case UI_SCREEN_MENU:
		return menu_event(ui, p, ev);
	case UI_SCREEN_DROP:
		if (ev->type == PLATFORM_EV_BACK) {
			ui->screen = UI_SCREEN_MENU;
		}
		break;
	case UI_SCREEN_SETTINGS:
		settings_event(ui, p, ev);
		break;
	case UI_SCREEN_JOYPAD:
		joypad_event(ui, p, ev);
		break;
	}
	return UI_ACTION_NONE;
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

	draw_mark(p, cx - 25, 56, 24);
	text_center(p, cx, 124, SCALE_HERO, t->text, "GBOYC");
	text_center(p, cx, 180, SCALE_BODY, t->dim, "Game Boy emulator");

	for (int i = 0; i < MENU_COUNT; i++) {
		draw_row(p, t, HERO_LIST_Y + i * ROW_PITCH, ROW_H, i == ui->selected, MENU_LABELS[i], NULL,
				 false);
	}

	draw_status(ui, p, t);
	draw_hints(p, t, HINTS_MENU, COUNT(HINTS_MENU));
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
		case SET_JOYPAD:
			label = "Joypad";
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
		draw_row(p, t, LIST_Y + i * ROW_PITCH, ROW_H, i == ui->row, label, value, adjustable);
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
	int y = LIST_Y + row * PAD_ROW_PITCH;
	bool selected = row == ui->row;

	if (row >= JOY_BUTTONS) {
		draw_row(p, t, y, PAD_ROW_H, selected, row == PAD_RESET ? "Reset to default" : "Back", NULL,
				 false);
		return;
	}

	fill_card(p, MARGIN_X, y, CONTENT_W, PAD_ROW_H, selected ? t->accent : t->surface);
	text_left(p, MARGIN_X + ROW_PAD, y + (PAD_ROW_H - GLYPH * SCALE_BODY) / 2, SCALE_BODY,
			  selected ? t->on_accent : t->text, PAD_BUTTONS[row].name);

	int button = PAD_BUTTONS[row].index;

	for (int s = 0; s < JOY_KEY_SLOTS; s++) {
		char name[16];
		bool active = selected && s == ui->slot;

		key_label(ui->settings->keymap.key[button][s], name, sizeof name);
		if (ui->capturing && active) {
			snprintf(name, sizeof name, "...");
		}
		draw_key_cell(p, t, PAD_COL_X + s * (PAD_CELL_W + PAD_CELL_GAP), y, selected, active, name);
	}
}

static void draw_joypad(const Ui *ui, Platform *p, const Theme *t) {
	draw_header(p, t, "Joypad");

	for (int s = 0; s < JOY_KEY_SLOTS; s++) {
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
		draw_hints(p, t, HINTS_JOYPAD, COUNT(HINTS_JOYPAD));
	}
}

void ui_draw(const Ui *ui, Platform *p) {
	const Theme *t = theme_get(ui->settings->theme, ui->system_dark);

	platform_set_background(p, t->bg);
	platform_clear(p, t->bg);

	switch (ui->screen) {
	case UI_SCREEN_MENU:
		draw_menu(ui, p, t);
		break;
	case UI_SCREEN_DROP:
		draw_drop(p, t);
		break;
	case UI_SCREEN_SETTINGS:
		draw_settings(ui, p, t);
		break;
	case UI_SCREEN_JOYPAD:
		draw_joypad(ui, p, t);
		break;
	}

	platform_flip(p);
}

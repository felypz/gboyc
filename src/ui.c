#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "boot.h"
#include "palette.h"

#define MENU_COUNT	  3
#define MENU_LOAD_ROM 0
#define MENU_SETTINGS 1
#define MENU_QUIT	  2

#define MENU_SCALE 2
#define MENU_X	   ((UI_VIEW_W - 10 * 8 * MENU_SCALE) / 2)

#define SETTINGS_COUNT 4
#define SET_BOOT	   0
#define SET_BOOT_FILE  1
#define SET_JOYPAD	   2
#define SET_BACK	   3

#define PAD_ROWS  (JOY_BUTTONS + 2)
#define PAD_RESET JOY_BUTTONS
#define PAD_BACK  (JOY_BUTTONS + 1)

static const char *const MENU_LABELS[MENU_COUNT] = {"Load ROM", "Settings", "Quit"};

static const struct {
	const char *name;
	int index;
} PAD_BUTTONS[JOY_BUTTONS] = {
	{"Up", 2}, {"Down", 3}, {"Left", 1},   {"Right", 0},
	{"A", 4},  {"B", 5},	{"Select", 6}, {"Start", 7},
};

static void set_status(Ui *ui, const char *text) {
	snprintf(ui->status, sizeof ui->status, "%s", text);
}

static void save(Ui *ui) {
	if (!settings_save(ui->settings)) {
		set_status(ui, "Could not save settings");
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

static const char *key_label(int scancode) {
	if (scancode <= 0) {
		return "-";
	}

	const char *name = platform_key_name(scancode);

	return (name && *name) ? name : "?";
}

void ui_init(Ui *ui, Settings *settings) {
	memset(ui, 0, sizeof *ui);
	ui->settings = settings;
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
		set_status(ui, "No boot ROM found");
		return;
	}

	ui->settings->boot_enabled = !ui->settings->boot_enabled;
	ui->status[0] = '\0';
	save(ui);
}

static void import_boot(Ui *ui, const char *path) {
	char name[SETTINGS_NAME_MAX];
	BootResult result = boot_import(path, name, sizeof name);

	if (result != BOOT_OK) {
		set_status(ui, boot_result_text(result));
		return;
	}

	snprintf(ui->settings->boot_file, sizeof ui->settings->boot_file, "%s", name);
	ui->settings->boot_enabled = true;
	ui->boot_ok = true;
	set_status(ui, "Boot ROM imported");
	save(ui);
}

static void stop_capture(Ui *ui, Platform *p, const char *status) {
	ui->capturing = false;
	platform_capture_keys(p, false);
	set_status(ui, status);
}

static void capture_key(Ui *ui, Platform *p, const PlatformEvent *ev) {
	int button = PAD_BUTTONS[ui->row].index;
	JoyKeymap *keymap = &ui->settings->keymap;

	if (ev->key <= 0 || !platform_key_valid(ev->key)) {
		return;
	}
	if (ev->hotkey) {
		snprintf(ui->status, sizeof ui->status, "In use by emulator: %s", ev->hotkey);
		return;
	}

	for (int b = 0; b < JOY_BUTTONS; b++) {
		for (int s = 0; s < JOY_KEY_SLOTS; s++) {
			if (keymap->key[b][s] == ev->key && !(b == button && s == ui->slot)) {
				snprintf(ui->status, sizeof ui->status, "Already bound to %s", button_name(b));
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
		set_status(ui, "Defaults restored");
		save(ui);
		return;
	}

	ui->capturing = true;
	platform_capture_keys(p, true);
	snprintf(ui->status, sizeof ui->status, "Press a key for %s (Esc: cancel)",
			 PAD_BUTTONS[ui->row].name);
}

static void pad_clear(Ui *ui) {
	if (ui->row >= JOY_BUTTONS) {
		return;
	}

	ui->settings->keymap.key[PAD_BUTTONS[ui->row].index][ui->slot] = 0;
	set_status(ui, "Key cleared");
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
		break;
	case PLATFORM_EV_DOWN:
		ui->row = (ui->row + 1) % PAD_ROWS;
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
		break;
	case PLATFORM_EV_DOWN:
		ui->row = (ui->row + 1) % SETTINGS_COUNT;
		break;
	case PLATFORM_EV_LEFT:
	case PLATFORM_EV_RIGHT:
		if (ui->row == SET_BOOT) {
			toggle_boot(ui);
		}
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
		set_status(ui, "File dialog unavailable");
	} else {
		ui->screen = UI_SCREEN_DROP;
	}
}

UiAction ui_handle_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_QUIT:
		return UI_ACTION_QUIT;
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

static void draw_menu(const Ui *ui, Platform *p) {
	platform_draw_text_centered(p, 40, 4, DMG_COLOR_3, "GBOYC");

	for (int i = 0; i < MENU_COUNT; i++) {
		char line[32];
		bool sel = (i == ui->selected);

		snprintf(line, sizeof line, "%c %s", sel ? '>' : ' ', MENU_LABELS[i]);
		platform_draw_text(p, MENU_X, 130 + i * 28, MENU_SCALE, sel ? DMG_COLOR_3 : DMG_COLOR_2,
						   line);
	}

	platform_draw_text_centered(p, 260, 1, DMG_COLOR_2,
								ui->dialog_open ? "Waiting for file dialog..." : "UP/DOWN + ENTER");
}

static void draw_drop(Platform *p) {
	platform_draw_text_centered(p, 100, 2, DMG_COLOR_3, "Drop the ROM");
	platform_draw_text_centered(p, 122, 2, DMG_COLOR_3, "file here");
	platform_draw_text_centered(p, 180, 1, DMG_COLOR_2, "Press ESC to go back");
}

static void setting_label(const Ui *ui, int row, char *out, size_t size) {
	switch (row) {
	case SET_BOOT:
		if (!ui->boot_ok) {
			snprintf(out, size, "Boot animation: OFF (no boot ROM)");
		} else {
			snprintf(out, size, "Boot animation: %s", ui->settings->boot_enabled ? "ON" : "OFF");
		}
		break;
	case SET_BOOT_FILE:
		snprintf(out, size, "Select boot ROM...");
		break;
	case SET_JOYPAD:
		snprintf(out, size, "Joypad...");
		break;
	default:
		snprintf(out, size, "Back");
		break;
	}
}

static void draw_status(const Ui *ui, Platform *p, int y) {
	if (ui->status[0]) {
		platform_draw_text(p, 8, y, 1, DMG_COLOR_3, ui->status);
	}
}

static void draw_settings(const Ui *ui, Platform *p) {
	platform_draw_text_centered(p, 20, 2, DMG_COLOR_3, "SETTINGS");

	for (int i = 0; i < SETTINGS_COUNT; i++) {
		char label[40];
		char line[48];

		setting_label(ui, i, label, sizeof label);
		snprintf(line, sizeof line, "%c %s", i == ui->row ? '>' : ' ', label);
		platform_draw_text(p, 8, 70 + i * 24, 1, i == ui->row ? DMG_COLOR_3 : DMG_COLOR_2, line);
	}

	draw_status(ui, p, 220);
	platform_draw_text(p, 8, 262, 1, DMG_COLOR_2,
					   ui->dialog_open ? "Waiting for file dialog..."
									   : "UP/DOWN + ENTER, ESC: back");
}

static void draw_pad_row(const Ui *ui, Platform *p, int row) {
	char line[64];
	int y = 44 + row * 20;
	uint32_t color = row == ui->row ? DMG_COLOR_3 : DMG_COLOR_2;
	char prefix = row == ui->row ? '>' : ' ';

	if (row == PAD_RESET) {
		snprintf(line, sizeof line, "%c Reset to default", prefix);
	} else if (row == PAD_BACK) {
		snprintf(line, sizeof line, "%c Back", prefix);
	} else {
		int button = PAD_BUTTONS[row].index;
		char cell[JOY_KEY_SLOTS][24];

		for (int s = 0; s < JOY_KEY_SLOTS; s++) {
			bool current = row == ui->row && s == ui->slot;
			const char *name = key_label(ui->settings->keymap.key[button][s]);

			if (ui->capturing && current) {
				name = "...";
			}
			snprintf(cell[s], sizeof cell[s], current ? "[%.12s]" : " %.12s ", name);
		}
		snprintf(line, sizeof line, "%c %-7s%-14s%-14s", prefix, PAD_BUTTONS[row].name, cell[0],
				 cell[1]);
	}
	platform_draw_text(p, 8, y, 1, color, line);
}

static void draw_joypad(const Ui *ui, Platform *p) {
	platform_draw_text_centered(p, 12, 2, DMG_COLOR_3, "JOYPAD");

	for (int row = 0; row < PAD_ROWS; row++) {
		draw_pad_row(ui, p, row);
	}

	draw_status(ui, p, 244);
	platform_draw_text(p, 8, 260, 1, DMG_COLOR_2, "ENTER: rebind  DEL: clear  L/R: slot");
	platform_draw_text(p, 8, 272, 1, DMG_COLOR_2, "ESC: back");
}

void ui_draw(const Ui *ui, Platform *p) {
	platform_clear(p, DMG_COLOR_0);

	switch (ui->screen) {
	case UI_SCREEN_MENU:
		draw_menu(ui, p);
		break;
	case UI_SCREEN_DROP:
		draw_drop(p);
		break;
	case UI_SCREEN_SETTINGS:
		draw_settings(ui, p);
		break;
	case UI_SCREEN_JOYPAD:
		draw_joypad(ui, p);
		break;
	}

	platform_flip(p);
}

#include "ui.h"

#include <stdio.h>

#include "palette.h"

#define MENU_COUNT	  2
#define MENU_LOAD_ROM 0
#define MENU_QUIT	  1

#define MENU_SCALE 2
#define MENU_X	   ((UI_VIEW_W - 10 * 8 * MENU_SCALE) / 2)

static const char *const MENU_LABELS[MENU_COUNT] = {"Load ROM", "Quit"};

void ui_init(Ui *ui) {
	ui->screen = UI_SCREEN_MENU;
	ui->selected = 0;
	ui->dialog_open = false;
	ui->rom_path[0] = '\0';
}

UiAction ui_handle_event(Ui *ui, Platform *p, const PlatformEvent *ev) {
	switch (ev->type) {
	case PLATFORM_EV_QUIT:
		return UI_ACTION_QUIT;

	case PLATFORM_EV_FILE_CHOSEN:
		ui->dialog_open = false;
		snprintf(ui->rom_path, sizeof ui->rom_path, "%s", ev->path);
		return UI_ACTION_LOAD_ROM;

	case PLATFORM_EV_DIALOG_CANCELED:
		ui->dialog_open = false;
		break;

	case PLATFORM_EV_DIALOG_FAILED:
		ui->dialog_open = false;
		ui->screen = UI_SCREEN_DROP;
		break;

	case PLATFORM_EV_UP:
		if (ui->screen == UI_SCREEN_MENU && !ui->dialog_open) {
			ui->selected = (ui->selected + MENU_COUNT - 1) % MENU_COUNT;
		}
		break;

	case PLATFORM_EV_DOWN:
		if (ui->screen == UI_SCREEN_MENU && !ui->dialog_open) {
			ui->selected = (ui->selected + 1) % MENU_COUNT;
		}
		break;

	case PLATFORM_EV_CONFIRM:
		if (ui->screen != UI_SCREEN_MENU || ui->dialog_open) {
			break;
		}
		if (ui->selected == MENU_QUIT) {
			return UI_ACTION_QUIT;
		}
		ui->dialog_open = true;
		platform_open_file_dialog(p);
		break;

	case PLATFORM_EV_BACK:
		if (ui->screen == UI_SCREEN_DROP) {
			ui->screen = UI_SCREEN_MENU;
		}
		break;
	default:
		break;
	}
	return UI_ACTION_NONE;
}

void ui_draw(const Ui *ui, Platform *p) {
	platform_clear(p, DMG_COLOR_0);

	if (ui->screen == UI_SCREEN_DROP) {
		platform_draw_text_centered(p, 100, 2, DMG_COLOR_3, "Drop the ROM");
		platform_draw_text_centered(p, 122, 2, DMG_COLOR_3, "file here");
		platform_draw_text_centered(p, 180, 1, DMG_COLOR_2, "Press ESC to go back");
	} else {
		platform_draw_text_centered(p, 40, 4, DMG_COLOR_3, "GBOYC");

		for (int i = 0; i < MENU_COUNT; i++) {
			char line[32];
			bool sel = (i == ui->selected);
			snprintf(line, sizeof line, "%c %s", sel ? '>' : ' ', MENU_LABELS[i]);
			platform_draw_text(p, MENU_X, 130 + i * 28, MENU_SCALE, sel ? DMG_COLOR_3 : DMG_COLOR_2,
							   line);
		}

		platform_draw_text_centered(p, 260, 1, DMG_COLOR_2,
									ui->dialog_open ? "Waiting for file dialog..."
													: "UP/DOWN + ENTER");
	}

	platform_flip(p);
}

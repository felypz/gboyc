#ifndef GBOYC_UI_H
#define GBOYC_UI_H

#include <stdbool.h>

#include "platform.h"
#include "settings.h"

typedef enum {
	UI_SCREEN_MENU,
	UI_SCREEN_DROP,
	UI_SCREEN_SETTINGS,
	UI_SCREEN_JOYPAD,
} UiScreen;

typedef enum {
	UI_ACTION_NONE,
	UI_ACTION_QUIT,
	UI_ACTION_LOAD_ROM,
} UiAction;

typedef struct {
	UiScreen screen;
	int selected;
	int row;
	int slot;
	bool capturing;
	bool dialog_open;
	bool dialog_boot;
	bool boot_ok;
	char status[48];
	char rom_path[PLATFORM_PATH_MAX];
	Settings *settings;
} Ui;

void ui_init(Ui *ui, Settings *settings);
UiAction ui_handle_event(Ui *ui, Platform *p, const PlatformEvent *ev);
void ui_draw(const Ui *ui, Platform *p);

#endif

#ifndef GBOYC_UI_H
#define GBOYC_UI_H

#include <stdbool.h>

#include "platform.h"
#include "settings.h"

typedef enum {
	UI_SCREEN_MENU,
	UI_SCREEN_DROP,
	UI_SCREEN_SETTINGS,
	UI_SCREEN_CONTROLS,
	UI_SCREEN_SHORTCUTS,
	UI_SCREEN_PAUSE,
	UI_SCREEN_COUNT,
} UiScreen;

typedef enum {
	UI_ACTION_NONE,
	UI_ACTION_QUIT,
	UI_ACTION_LOAD_ROM,
	UI_ACTION_RESUME,
	UI_ACTION_RESET,
	UI_ACTION_MAIN_MENU,
} UiAction;

typedef struct {
	UiScreen screen;
	UiScreen parent;
	int row;
	int sel[UI_SCREEN_COUNT];
	int slot;
	bool capturing;
	bool dialog_open;
	bool dialog_boot;
	bool boot_ok;
	bool system_dark;
	bool status_warn;
	bool in_game;
	char status[48];
	char rom_path[PLATFORM_PATH_MAX];
	Settings *settings;
} Ui;

typedef struct {
	const char *toast;
	int speed;
	bool paused;
	unsigned pc;
	bool mid_instruction;
	unsigned cycles;
} Hud;

void ui_init(Ui *ui, Settings *settings);
void ui_open_pause(Ui *ui);
void ui_notify(Ui *ui, const char *text, bool warn);
UiAction ui_handle_event(Ui *ui, Platform *p, const PlatformEvent *ev);
void ui_draw(const Ui *ui, Platform *p);
void ui_draw_hud(const Ui *ui, Platform *p, const Hud *hud);

#endif

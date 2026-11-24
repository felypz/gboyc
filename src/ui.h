#ifndef GBOYC_UI_H
#define GBOYC_UI_H

#include <stdbool.h>

#include "crash_report.h"
#include "platform.h"
#include "settings.h"

typedef enum {
	UI_SCREEN_MENU,
	UI_SCREEN_ROM_DROP,
	UI_SCREEN_SETTINGS,
	UI_SCREEN_KEYBOARD, /* "Controls": the selected player's keyboard binds */
	UI_SCREEN_GAMEPAD,	/* "Controls": the selected player's gamepad binds */
	UI_SCREEN_PAD_TEST,
	UI_SCREEN_SHORTCUTS,
	UI_SCREEN_PAUSE,
	UI_SCREEN_CRASH,
	UI_SCREEN_COUNT,
} UiScreen;

typedef enum {
	UI_ACTION_NONE,
	UI_ACTION_QUIT,
	UI_ACTION_LOAD_ROM,
	UI_ACTION_LOAD_STATE,
	UI_ACTION_RESUME,
	UI_ACTION_RESET,
	UI_ACTION_MAIN_MENU,
} UiAction;

typedef struct {
	UiScreen screen;
	UiScreen parent;
	int row;
	int sel[UI_SCREEN_COUNT];
	int player;			 /* whose binds the keyboard / gamepad screens edit (0 or 1) */
	int test_pad;		 /* gamepad shown on the controller test screen */
	bool prefer_gamepad; /* which device tab opens first; survives reset */
	bool capturing;
	bool bind_all;		  /* "bind all" is walking through the items of this screen */
	Bindings bind_backup; /* restored if bind all is cancelled */
	bool dialog_open;
	PlatformDialog dialog_kind;
	bool boot_rom_installed;
	bool system_dark;
	bool status_warn;
	bool in_game;
	char status[48];
	char rom_path[PATHS_MAX];
	CrashReport crash;
	Settings *settings;
} Ui;

typedef struct {
	const char *toast;
	int speed;
	bool paused;
	unsigned pc;
	bool mid_instruction;
	unsigned mcycles;
} Hud;

void ui_init(Ui *ui, Settings *settings);
void ui_open_pause(Ui *ui);
void ui_open_crash(Ui *ui, const CrashReport *report);
void ui_notify(Ui *ui, const char *text, bool warn);
void ui_set_system_dark(Ui *ui, bool dark);
/* ROM path chosen with UiAction UI_ACTION_LOAD_ROM. */
const char *ui_selected_rom(const Ui *ui);
UiAction ui_handle_event(Ui *ui, Platform *p, const PlatformEvent *ev);
void ui_draw(const Ui *ui, Platform *p);
void ui_draw_hud(const Ui *ui, Platform *p, const Hud *hud);

#endif

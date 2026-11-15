#ifndef GBOYC_UI_H
#define GBOYC_UI_H

#include <stdbool.h>

#include "platform.h"

typedef enum {
	UI_SCREEN_MENU,
	UI_SCREEN_DROP,
} UiScreen;

typedef enum {
	UI_ACTION_NONE,
	UI_ACTION_QUIT,
	UI_ACTION_LOAD_ROM,
} UiAction;

typedef struct {
	UiScreen screen;
	int selected;
	bool dialog_open;
	char rom_path[PLATFORM_PATH_MAX];
} Ui;

void ui_init(Ui *ui);
UiAction ui_handle_event(Ui *ui, Platform *p, const PlatformEvent *ev);
void ui_draw(const Ui *ui, Platform *p);

#endif

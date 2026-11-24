#include "platform.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu.h"
#include "log.h"
#include "paths.h"
#include "timing.h"

#define NS_PER_SECOND		   1000000000ULL
#define FRAME_NS			   (GB_DOTS_PER_FRAME * NS_PER_SECOND / GB_CPU_HZ)
#define AUDIO_CHANNELS		   2
#define AUDIO_FRAME_BYTES	   (AUDIO_CHANNELS * (int)sizeof(float))
#define AUDIO_MAX_QUEUED_BYTES (APU_SAMPLE_RATE / 10 * AUDIO_FRAME_BYTES)
#define AUDIO_PRIME_FRAMES	   1024

#define MAX_GAMEPADS	4
#define GAMEPAD_DB_FILE "gamecontrollerdb.txt"

_Static_assert(PLATFORM_GLYPH_W == SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE &&
				   PLATFORM_GLYPH_H == SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,
			   "PLATFORM_GLYPH_* must match SDL's debug font");

/* Result codes carried in the user event that reports a file dialog outcome. */
enum { DIALOG_CHOSEN, DIALOG_CANCELED, DIALOG_FAILED };

struct Platform {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *frame_texture[MAX_PLAYERS]; /* PPU_W x PPU_H, streamed from a framebuffer */
	SDL_Texture *ui_target; /* UI_VIEW_W x UI_VIEW_H, everything is drawn here first */
	SDL_AudioStream *audio;
	uint64_t next_frame_ns;
	Uint32 dialog_event;
	PlatformCapture capture;
	bool chord_latched; /* Back + Start already reported; released with either button */
	uint32_t background;
	SDL_Gamepad *gamepads[MAX_GAMEPADS]; /* NULL-terminated by count, in connection order */
	int gamepad_count;
};

static void set_color(Platform *p, uint32_t argb) {
	SDL_SetRenderDrawColor(p->renderer, (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
						   (argb >> 24) & 0xFF);
}

static void use_ui_target(Platform *p) {
	if (SDL_GetRenderTarget(p->renderer) != p->ui_target) {
		SDL_SetRenderTarget(p->renderer, p->ui_target);
	}
}

/* Largest integer-scaled (or, in tiny windows, fractional) centered rect for the UI canvas. */
static SDL_FRect present_rect(Platform *p) {
	int w = 0;
	int h = 0;

	SDL_GetRenderOutputSize(p->renderer, &w, &h);

	int scale = w / PPU_W;
	if (h / PPU_H < scale) {
		scale = h / PPU_H;
	}

	float rw;
	float rh;

	if (scale >= 1) {
		rw = (float)(PPU_W * scale);
		rh = (float)(PPU_H * scale);
	} else {
		float sx = (float)w / PPU_W;
		float sy = (float)h / PPU_H;
		float s = sx < sy ? sx : sy;

		rw = PPU_W * s;
		rh = PPU_H * s;
	}

	SDL_FRect rect = {(float)(int)(((float)w - rw) / 2), (float)(int)(((float)h - rh) / 2), rw, rh};

	return rect;
}

static void window_to_ui(Platform *p, float wx, float wy, int *x, int *y) {
	float rx = 0;
	float ry = 0;

	SDL_RenderCoordinatesFromWindow(p->renderer, wx, wy, &rx, &ry);

	SDL_FRect rect = present_rect(p);

	*x = (int)((rx - rect.x) * UI_VIEW_W / rect.w);
	*y = (int)((ry - rect.y) * UI_VIEW_H / rect.h);
}

static void audio_open(Platform *p) {
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
		return;
	}

	SDL_AudioSpec spec = {SDL_AUDIO_F32, AUDIO_CHANNELS, APU_SAMPLE_RATE};
	p->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (!p->audio) {
		fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
		return;
	}
	SDL_ResumeAudioStreamDevice(p->audio);
}

/* -- Gamepads -- */

/*
 * SDL maps most pads (Xbox, PlayStation, Switch, many generic ones) to one standard layout.
 * Pads SDL doesn't know can be described in "<config dir>/gamecontrollerdb.txt" (same format
 * as SDL_GameControllerDB) or in the SDL_GAMECONTROLLERCONFIG environment variable.
 */
static void gamepad_init(void) {
	char path[PATHS_MAX];

	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "gamepads disabled: %s\n", SDL_GetError());
		return;
	}
	if (paths_file(PATHS_CONFIG, path, sizeof path, NULL, GAMEPAD_DB_FILE)) {
		int added = SDL_AddGamepadMappingsFromFile(path);

		if (added > 0) {
			log_verbose("Gamepad mappings: %d from %s", added, path);
		}
	}
}

static int gamepad_find(const Platform *p, SDL_JoystickID id) {
	for (int i = 0; i < p->gamepad_count; i++) {
		if (SDL_GetGamepadID(p->gamepads[i]) == id) {
			return i;
		}
	}
	return -1;
}

static bool gamepad_add(Platform *p, SDL_JoystickID id, PlatformEvent *out) {
	if (p->gamepad_count >= MAX_GAMEPADS || gamepad_find(p, id) >= 0) {
		return false;
	}

	SDL_Gamepad *pad = SDL_OpenGamepad(id);

	if (!pad) {
		log_verbose("Could not open gamepad %u: %s", (unsigned)id, SDL_GetError());
		return false;
	}

	const char *name = SDL_GetGamepadName(pad);

	p->gamepads[p->gamepad_count++] = pad;
	out->type = PLATFORM_EV_GAMEPAD_ADDED;
	snprintf(out->path, sizeof out->path, "%s", name ? name : "Gamepad");
	return true;
}

static bool gamepad_remove(Platform *p, SDL_JoystickID id, PlatformEvent *out) {
	int i = gamepad_find(p, id);

	if (i < 0) {
		return false;
	}

	const char *name = SDL_GetGamepadName(p->gamepads[i]);

	out->type = PLATFORM_EV_GAMEPAD_REMOVED;
	snprintf(out->path, sizeof out->path, "%s", name ? name : "Gamepad");
	SDL_CloseGamepad(p->gamepads[i]);
	memmove(&p->gamepads[i], &p->gamepads[i + 1],
			(size_t)(p->gamepad_count - i - 1) * sizeof p->gamepads[0]);
	p->gamepads[--p->gamepad_count] = NULL;
	return true;
}

int platform_gamepad_count(const Platform *p) {
	return p->gamepad_count;
}

_Static_assert((int)PLATFORM_BTN_DPAD_RIGHT == (int)SDL_GAMEPAD_BUTTON_DPAD_RIGHT &&
				   (int)PLATFORM_BTN_LEFT_BUMPER == (int)SDL_GAMEPAD_BUTTON_LEFT_SHOULDER &&
				   (int)PLATFORM_BTN_START == (int)SDL_GAMEPAD_BUTTON_START &&
				   (int)PLATFORM_BTN_RIGHT_STICK == (int)SDL_GAMEPAD_BUTTON_RIGHT_STICK,
			   "platform button numbers follow SDL's");

bool platform_gamepad_state(const Platform *p, int index, PlatformPadState *out) {
	static const SDL_GamepadAxis AXES[PLATFORM_PAD_AXES] = {
		SDL_GAMEPAD_AXIS_LEFTX,	 SDL_GAMEPAD_AXIS_LEFTY,		SDL_GAMEPAD_AXIS_RIGHTX,
		SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};

	if (index < 0 || index >= p->gamepad_count) {
		return false;
	}
	out->buttons = 0;
	for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) {
		if (SDL_GetGamepadButton(p->gamepads[index], (SDL_GamepadButton)i)) {
			out->buttons |= 1u << i;
		}
	}
	for (int i = 0; i < PLATFORM_PAD_AXES; i++) {
		out->axes[i] = SDL_GetGamepadAxis(p->gamepads[index], AXES[i]);
	}
	return true;
}

const char *platform_gamepad_name(const Platform *p, int index) {
	return index >= 0 && index < p->gamepad_count ? SDL_GetGamepadName(p->gamepads[index]) : NULL;
}

/* Position-based names: the face buttons are labelled differently on every brand. */
static const char *const BUTTON_NAMES[SDL_GAMEPAD_BUTTON_COUNT] = {
	[SDL_GAMEPAD_BUTTON_SOUTH] = "South",
	[SDL_GAMEPAD_BUTTON_EAST] = "East",
	[SDL_GAMEPAD_BUTTON_WEST] = "West",
	[SDL_GAMEPAD_BUTTON_NORTH] = "North",
	[SDL_GAMEPAD_BUTTON_BACK] = "Back",
	[SDL_GAMEPAD_BUTTON_GUIDE] = "Guide",
	[SDL_GAMEPAD_BUTTON_START] = "Start",
	[SDL_GAMEPAD_BUTTON_LEFT_STICK] = "L-Stick",
	[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = "R-Stick",
	[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = "L-Bumper",
	[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = "R-Bumper",
	[SDL_GAMEPAD_BUTTON_DPAD_UP] = "D-Up",
	[SDL_GAMEPAD_BUTTON_DPAD_DOWN] = "D-Down",
	[SDL_GAMEPAD_BUTTON_DPAD_LEFT] = "D-Left",
	[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = "D-Right",
	[SDL_GAMEPAD_BUTTON_MISC1] = "Misc 1",
	[SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1] = "R-Paddle 1",
	[SDL_GAMEPAD_BUTTON_LEFT_PADDLE1] = "L-Paddle 1",
	[SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2] = "R-Paddle 2",
	[SDL_GAMEPAD_BUTTON_LEFT_PADDLE2] = "L-Paddle 2",
	[SDL_GAMEPAD_BUTTON_TOUCHPAD] = "Touchpad",
	[SDL_GAMEPAD_BUTTON_MISC2] = "Misc 2",
	[SDL_GAMEPAD_BUTTON_MISC3] = "Misc 3",
	[SDL_GAMEPAD_BUTTON_MISC4] = "Misc 4",
	[SDL_GAMEPAD_BUTTON_MISC5] = "Misc 5",
	[SDL_GAMEPAD_BUTTON_MISC6] = "Misc 6",
};

bool platform_gamepad_code_valid(int code) {
	return code >= GAMEPAD_NONE && code <= SDL_GAMEPAD_BUTTON_COUNT;
}

const char *platform_gamepad_button_name(int code) {
	if (code <= GAMEPAD_NONE || code > SDL_GAMEPAD_BUTTON_COUNT) {
		return NULL;
	}
	return BUTTON_NAMES[code - 1];
}

bool platform_gamepad_nav(int code, PlatformEventType *nav) {
	switch ((SDL_GamepadButton)(code - 1)) {
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		*nav = PLATFORM_EV_UP;
		return true;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		*nav = PLATFORM_EV_DOWN;
		return true;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		*nav = PLATFORM_EV_LEFT;
		return true;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		*nav = PLATFORM_EV_RIGHT;
		return true;
	case SDL_GAMEPAD_BUTTON_SOUTH:
	case SDL_GAMEPAD_BUTTON_START:
		*nav = PLATFORM_EV_CONFIRM;
		return true;
	case SDL_GAMEPAD_BUTTON_EAST:
	case SDL_GAMEPAD_BUTTON_BACK:
		*nav = PLATFORM_EV_BACK;
		return true;
	case SDL_GAMEPAD_BUTTON_WEST:
		*nav = PLATFORM_EV_CLEAR;
		return true;
	case SDL_GAMEPAD_BUTTON_NORTH:
		*nav = PLATFORM_EV_TEST;
		return true;
	default:
		return false;
	}
}

Platform *platform_create(const char *title, int scale, bool audio) {
	Platform *p = calloc(1, sizeof *p);
	if (!p) {
		return NULL;
	}
	p->background = 0xFF000000;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		goto fail;
	}

	gamepad_init();

	p->dialog_event = SDL_RegisterEvents(1);
	if (p->dialog_event == (Uint32)-1) {
		fprintf(stderr, "SDL_RegisterEvents: %s\n", SDL_GetError());
		goto fail;
	}

	p->window = SDL_CreateWindow(title, PPU_W * scale, PPU_H * scale, SDL_WINDOW_RESIZABLE);
	if (!p->window) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		goto fail;
	}

	p->renderer = SDL_CreateRenderer(p->window, NULL);
	if (!p->renderer) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		goto fail;
	}
	SDL_SetRenderDrawBlendMode(p->renderer, SDL_BLENDMODE_BLEND);

	/* One texture per console: an update takes effect immediately, but draws are queued. */
	for (int i = 0; i < MAX_PLAYERS; i++) {
		p->frame_texture[i] = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888,
												SDL_TEXTUREACCESS_STREAMING, PPU_W, PPU_H);
		if (!p->frame_texture[i]) {
			fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
			goto fail;
		}
		SDL_SetTextureScaleMode(p->frame_texture[i], SDL_SCALEMODE_NEAREST);
	}

	p->ui_target = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888,
									 SDL_TEXTUREACCESS_TARGET, UI_VIEW_W, UI_VIEW_H);
	if (!p->ui_target) {
		fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
		goto fail;
	}
	SDL_SetTextureScaleMode(p->ui_target, SDL_SCALEMODE_NEAREST);

	if (audio) {
		audio_open(p);
	}

	p->next_frame_ns = SDL_GetTicksNS();
	return p;

fail:
	platform_destroy(p);
	return NULL;
}

void platform_destroy(Platform *p) {
	if (!p) {
		return;
	}
	if (p->audio) {
		SDL_DestroyAudioStream(p->audio);
	}
	for (int i = 0; i < p->gamepad_count; i++) {
		SDL_CloseGamepad(p->gamepads[i]);
	}
	if (p->ui_target) {
		SDL_DestroyTexture(p->ui_target);
	}
	for (int i = 0; i < MAX_PLAYERS; i++) {
		if (p->frame_texture[i]) {
			SDL_DestroyTexture(p->frame_texture[i]);
		}
	}
	if (p->renderer) {
		SDL_DestroyRenderer(p->renderer);
	}
	if (p->window) {
		SDL_DestroyWindow(p->window);
	}
	SDL_Quit();
	free(p);
}

void platform_set_background(Platform *p, uint32_t argb) {
	p->background = argb;
}

static PlatformEventType map_nav(SDL_Keycode key) {
	switch (key) {
	case SDLK_UP:
		return PLATFORM_EV_UP;
	case SDLK_DOWN:
		return PLATFORM_EV_DOWN;
	case SDLK_LEFT:
		return PLATFORM_EV_LEFT;
	case SDLK_RIGHT:
		return PLATFORM_EV_RIGHT;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
	case SDLK_SPACE:
		return PLATFORM_EV_CONFIRM;
	case SDLK_ESCAPE:
		return PLATFORM_EV_BACK;
	case SDLK_DELETE:
		return PLATFORM_EV_CLEAR;
	default:
		return PLATFORM_EV_KEY;
	}
}

/* Event for a key pressed while capturing: ESC always cancels, bind-all adds ENTER and DEL. */
static PlatformEventType capture_type(PlatformCapture mode, SDL_Keycode key) {
	if (key == SDLK_ESCAPE) {
		return PLATFORM_EV_BACK;
	}
	if (mode == PLATFORM_CAPTURE_SEQUENCE) {
		if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
			return PLATFORM_EV_CONFIRM;
		}
		if (key == SDLK_DELETE) {
			return PLATFORM_EV_CLEAR;
		}
	}
	return PLATFORM_EV_KEY_CAPTURED;
}

/*
 * Back + Start together open the pause menu from a gamepad (and leave the controller test).
 * It is fixed rather than a bind, so it works whatever the buttons are bound to. Only the
 * press that completes the pair counts, and that press is not reported as a button.
 */
typedef enum { CHORD_NONE, CHORD_FIRE, CHORD_SWALLOW } ChordResult;

static ChordResult menu_chord(Platform *p, const SDL_Event *ev) {
	SDL_GamepadButton other;

	if (ev->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
		other = SDL_GAMEPAD_BUTTON_START;
	} else if (ev->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
		other = SDL_GAMEPAD_BUTTON_BACK;
	} else {
		return CHORD_NONE;
	}

	SDL_Gamepad *pad = SDL_GetGamepadFromID(ev->gbutton.which);

	if (!pad || !SDL_GetGamepadButton(pad, other)) {
		return CHORD_NONE;
	}
	/* Two presses polled in one batch both see the pair held; only the first one counts. */
	if (p->chord_latched) {
		return CHORD_SWALLOW;
	}
	p->chord_latched = true;
	return CHORD_FIRE;
}

bool platform_next_event(Platform *p, PlatformEvent *out) {
	SDL_Event ev;

	while (SDL_PollEvent(&ev)) {
		out->path[0] = '\0';
		out->key = 0;
		out->repeat = false;
		out->mx = 0;
		out->my = 0;

		if (ev.type == p->dialog_event) {
			char *path = ev.user.data1;
			if (ev.user.code == DIALOG_CHOSEN && path) {
				out->type = PLATFORM_EV_FILE_CHOSEN;
				snprintf(out->path, sizeof out->path, "%s", path);
			} else if (ev.user.code == DIALOG_CANCELED) {
				out->type = PLATFORM_EV_DIALOG_CANCELED;
			} else {
				out->type = PLATFORM_EV_DIALOG_FAILED;
			}
			SDL_free(path);
			return true;
		}

		switch (ev.type) {
		case SDL_EVENT_QUIT:
			out->type = PLATFORM_EV_QUIT;
			return true;
		case SDL_EVENT_SYSTEM_THEME_CHANGED:
			out->type = PLATFORM_EV_THEME_CHANGED;
			return true;
		case SDL_EVENT_KEY_DOWN:
			out->key = (int)ev.key.scancode;
			out->repeat = ev.key.repeat;
			if (p->capture == PLATFORM_CAPTURE_SINGLE || p->capture == PLATFORM_CAPTURE_SEQUENCE) {
				if (ev.key.repeat) {
					break;
				}
				out->type = capture_type(p->capture, ev.key.key);
				return true;
			}
			out->type = map_nav(ev.key.key);
			return true;
		case SDL_EVENT_GAMEPAD_ADDED:
			if (gamepad_add(p, ev.gdevice.which, out)) {
				return true;
			}
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			if (gamepad_remove(p, ev.gdevice.which, out)) {
				return true;
			}
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			if (p->capture == PLATFORM_CAPTURE_OFF || p->capture == PLATFORM_CAPTURE_MONITOR) {
				ChordResult chord = menu_chord(p, &ev);

				if (chord == CHORD_FIRE) {
					out->type = PLATFORM_EV_GAMEPAD_MENU;
					return true;
				}
				if (chord == CHORD_SWALLOW) {
					break;
				}
			}
			if (p->capture == PLATFORM_CAPTURE_MONITOR) {
				break;
			}
			out->key = (int)ev.gbutton.button + 1;
			out->type = p->capture != PLATFORM_CAPTURE_OFF ? PLATFORM_EV_GAMEPAD_CAPTURED
														   : PLATFORM_EV_GAMEPAD_BUTTON;
			return true;
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			if (ev.gbutton.button == SDL_GAMEPAD_BUTTON_BACK ||
				ev.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
				p->chord_latched = false;
			}
			break;
		case SDL_EVENT_MOUSE_MOTION:
			out->type = PLATFORM_EV_MOUSE_MOVE;
			window_to_ui(p, ev.motion.x, ev.motion.y, &out->mx, &out->my);
			return true;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (ev.button.button == SDL_BUTTON_LEFT) {
				out->type = PLATFORM_EV_MOUSE_CLICK;
				window_to_ui(p, ev.button.x, ev.button.y, &out->mx, &out->my);
				return true;
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			if (ev.wheel.y != 0) {
				out->type = ev.wheel.y > 0 ? PLATFORM_EV_UP : PLATFORM_EV_DOWN;
				return true;
			}
			break;
		case SDL_EVENT_DROP_FILE:
			if (ev.drop.data) {
				out->type = PLATFORM_EV_FILE_CHOSEN;
				snprintf(out->path, sizeof out->path, "%s", ev.drop.data);
				return true;
			}
			break;
		}
	}
	return false;
}

/* Player 1 gets the usual layout; player 2 starts with nothing bound. */
void platform_default_pad(Bindings *b, int player) {
	int *pad = b->pad[player];

	memset(pad, 0, sizeof b->pad[player]);
	if (player != 0) {
		return;
	}
	pad[JOY_BTN_RIGHT] = SDL_SCANCODE_RIGHT;
	pad[JOY_BTN_LEFT] = SDL_SCANCODE_LEFT;
	pad[JOY_BTN_UP] = SDL_SCANCODE_UP;
	pad[JOY_BTN_DOWN] = SDL_SCANCODE_DOWN;
	pad[JOY_BTN_A] = SDL_SCANCODE_Z;
	pad[JOY_BTN_B] = SDL_SCANCODE_X;
	pad[JOY_BTN_SELECT] = SDL_SCANCODE_BACKSPACE;
	pad[JOY_BTN_START] = SDL_SCANCODE_RETURN;
}

void platform_default_gamepad(Bindings *b, int player) {
	int *pad = b->gamepad[player];

	memset(pad, 0, sizeof b->gamepad[player]);
	if (player != 0) {
		return; /* player 2 starts with nothing bound */
	}
	pad[JOY_BTN_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
	pad[JOY_BTN_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT + 1;
	pad[JOY_BTN_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP + 1;
	pad[JOY_BTN_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN + 1;
	/* Same physical positions as on a Game Boy: A on the right, B below-left of it. */
	pad[JOY_BTN_A] = SDL_GAMEPAD_BUTTON_EAST + 1;
	pad[JOY_BTN_B] = SDL_GAMEPAD_BUTTON_SOUTH + 1;
	pad[JOY_BTN_SELECT] = SDL_GAMEPAD_BUTTON_BACK + 1;
	pad[JOY_BTN_START] = SDL_GAMEPAD_BUTTON_START + 1;
}

void platform_default_shortcuts(Bindings *b) {
	memset(b->shortcut, 0, sizeof b->shortcut);
	b->shortcut[SC_PAUSE] = SDL_SCANCODE_P;
	b->shortcut[SC_SPEED] = SDL_SCANCODE_TAB;
	b->shortcut[SC_RESET] = SDL_SCANCODE_R;
	b->shortcut[SC_SAVE_STATE] = SDL_SCANCODE_F5;
	b->shortcut[SC_LOAD_STATE] = SDL_SCANCODE_F9;
	b->shortcut[SC_STEP_FRAME] = SDL_SCANCODE_N;
	b->shortcut[SC_STEP_INSTR] = SDL_SCANCODE_I;
	b->shortcut[SC_STEP_MCYCLE] = SDL_SCANCODE_C;
}

const char *platform_key_name(int scancode) {
	return SDL_GetScancodeName((SDL_Scancode)scancode);
}

/* KEY_NONE (an unbound slot) counts as valid: it is what unbinding stores. */
int platform_test_key(void) {
	return SDL_SCANCODE_F1;
}

bool platform_key_valid(int scancode) {
	return scancode >= KEY_NONE && scancode < SDL_SCANCODE_COUNT;
}

int platform_slot_for_key(int scancode) {
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
		return scancode - SDL_SCANCODE_1 + 1;
	}
	return 0;
}

bool platform_key_reserved(int scancode) {
	return platform_slot_for_key(scancode) != 0 || scancode == platform_test_key();
}

void platform_capture_input(Platform *p, PlatformCapture mode) {
	p->capture = mode;
}

bool platform_system_dark(void) {
	return SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
}

void platform_show_cursor(bool show) {
	if (show) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}
}

static uint8_t stick_mask(SDL_Gamepad *pad) {
	int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
	int y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
	uint8_t mask = 0;

	if (x > PLATFORM_STICK_DEADZONE) {
		mask |= JOY_MASK(JOY_BTN_RIGHT);
	} else if (x < -PLATFORM_STICK_DEADZONE) {
		mask |= JOY_MASK(JOY_BTN_LEFT);
	}
	if (y > PLATFORM_STICK_DEADZONE) {
		mask |= JOY_MASK(JOY_BTN_DOWN);
	} else if (y < -PLATFORM_STICK_DEADZONE) {
		mask |= JOY_MASK(JOY_BTN_UP);
	}
	return mask;
}

static uint8_t gamepad_mask(SDL_Gamepad *pad, const int *codes) {
	uint8_t mask = stick_mask(pad);

	for (int button = 0; button < JOY_BTN_COUNT; button++) {
		int code = codes[button];

		if (code != GAMEPAD_NONE && SDL_GetGamepadButton(pad, code - 1)) {
			mask |= JOY_MASK(button);
		}
	}
	return mask;
}

uint8_t platform_joypad(const Platform *p, const Bindings *bindings, int player, int players) {
	const bool *keys = SDL_GetKeyboardState(NULL);
	uint8_t mask = 0;

	for (int button = 0; button < JOY_BTN_COUNT; button++) {
		int scancode = bindings->pad[player][button];

		if (scancode != KEY_NONE && platform_key_valid(scancode) && keys[scancode]) {
			mask |= JOY_MASK(button);
		}
	}
	for (int i = 0; i < p->gamepad_count; i++) {
		if (players == 1 || i == player) {
			mask |= gamepad_mask(p->gamepads[i], bindings->gamepad[player]);
		}
	}
	return mask;
}

static void SDLCALL on_file_dialog(void *userdata, const char *const *filelist, int filter) {
	(void)filter;

	SDL_Event ev;
	SDL_zero(ev);
	ev.type = (Uint32)(uintptr_t)userdata;

	if (!filelist) {
		ev.user.code = DIALOG_FAILED;
	} else if (!filelist[0]) {
		ev.user.code = DIALOG_CANCELED;
	} else {
		ev.user.code = DIALOG_CHOSEN;
		ev.user.data1 = SDL_strdup(filelist[0]);
	}

	if (!SDL_PushEvent(&ev)) {
		SDL_free(ev.user.data1);
	}
}

void platform_open_file_dialog(Platform *p, PlatformDialog kind) {
	static const SDL_DialogFileFilter rom_filters[] = {
		{"Game Boy ROMs", "gb;gbc"},
		{"All files", "*"},
	};
	static const SDL_DialogFileFilter boot_filters[] = {
		{"Boot ROM", "bin"},
		{"All files", "*"},
	};

	SDL_ShowOpenFileDialog(on_file_dialog, (void *)(uintptr_t)p->dialog_event, p->window,
						   kind == PLATFORM_DIALOG_BOOT_ROM ? boot_filters : rom_filters, 2, NULL,
						   false);
}

bool platform_make_dir(const char *path) {
	return SDL_CreateDirectory(path);
}

void platform_clear(Platform *p, uint32_t argb) {
	use_ui_target(p);
	set_color(p, argb);
	SDL_RenderClear(p->renderer);
}

void platform_fill_rect(Platform *p, int x, int y, int w, int h, uint32_t argb) {
	SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};

	use_ui_target(p);
	set_color(p, argb);
	SDL_RenderFillRect(p->renderer, &rect);
}

void platform_draw_text(Platform *p, int x, int y, int scale, uint32_t argb, const char *text) {
	use_ui_target(p);
	set_color(p, argb);
	SDL_SetRenderScale(p->renderer, (float)scale, (float)scale);
	SDL_RenderDebugText(p->renderer, (float)x / scale, (float)y / scale, text);
	SDL_SetRenderScale(p->renderer, 1.0f, 1.0f);
}

void platform_draw_frame(Platform *p, const uint32_t *framebuffer) {
	use_ui_target(p);
	SDL_UpdateTexture(p->frame_texture[0], NULL, framebuffer, PPU_W * (int)sizeof(uint32_t));
	SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);
	SDL_RenderClear(p->renderer);
	SDL_RenderTexture(p->renderer, p->frame_texture[0], NULL, NULL);
}

void platform_draw_frames(Platform *p, const uint32_t *left, const uint32_t *right) {
	const uint32_t *frames[MAX_PLAYERS] = {left, right};
	float w = UI_VIEW_W / MAX_PLAYERS;
	float h = w * PPU_H / PPU_W;

	use_ui_target(p);
	SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);
	SDL_RenderClear(p->renderer);
	for (int i = 0; i < MAX_PLAYERS; i++) {
		SDL_FRect dst = {w * (float)i, ((float)UI_VIEW_H - h) / 2, w, h};

		SDL_UpdateTexture(p->frame_texture[i], NULL, frames[i], PPU_W * (int)sizeof(uint32_t));
		SDL_RenderTexture(p->renderer, p->frame_texture[i], NULL, &dst);
	}
}

void platform_flip(Platform *p) {
	SDL_SetRenderTarget(p->renderer, NULL);

	SDL_FRect dst = present_rect(p);

	set_color(p, p->background);
	SDL_RenderClear(p->renderer);
	SDL_RenderTexture(p->renderer, p->ui_target, NULL, &dst);
	SDL_RenderPresent(p->renderer);
}

void platform_audio_push(Platform *p, const float *samples, size_t frames) {
	if (!p->audio || !frames) {
		return;
	}

	int queued = SDL_GetAudioStreamQueued(p->audio);
	if (queued > AUDIO_MAX_QUEUED_BYTES) {
		return;
	}

	if (queued == 0) {
		static const float silence[AUDIO_PRIME_FRAMES * AUDIO_CHANNELS];
		SDL_PutAudioStreamData(p->audio, silence, (int)sizeof silence);
	}
	SDL_PutAudioStreamData(p->audio, samples, (int)frames * AUDIO_FRAME_BYTES);
}

void platform_audio_clear(Platform *p) {
	if (p->audio) {
		SDL_ClearAudioStream(p->audio);
	}
}

uint64_t platform_ticks_ms(void) {
	return SDL_GetTicks();
}

void platform_frame_limit(Platform *p) {
	uint64_t now = SDL_GetTicksNS();
	if (p->next_frame_ns > now) {
		SDL_DelayNS(p->next_frame_ns - now);
	} else {
		p->next_frame_ns = now;
	}
	p->next_frame_ns += FRAME_NS;
}

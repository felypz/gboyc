#include "platform.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu.h"

#define GB_FRAME_NS			   16742706ULL
#define AUDIO_CHANNELS		   2
#define AUDIO_FRAME_BYTES	   (AUDIO_CHANNELS * (int)sizeof(float))
#define AUDIO_MAX_QUEUED_BYTES (APU_SAMPLE_RATE / 10 * AUDIO_FRAME_BYTES)
#define AUDIO_PRIME_FRAMES	   1024

enum { DIALOG_CHOSEN, DIALOG_CANCELED, DIALOG_FAILED };

struct Platform {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_Texture *screen;
	SDL_AudioStream *audio;
	uint64_t next_frame_ns;
	Uint32 dialog_event;
	bool capture;
	uint32_t background;
};

static void set_color(Platform *p, uint32_t argb) {
	SDL_SetRenderDrawColor(p->renderer, (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
						   (argb >> 24) & 0xFF);
}

static void use_screen(Platform *p) {
	if (SDL_GetRenderTarget(p->renderer) != p->screen) {
		SDL_SetRenderTarget(p->renderer, p->screen);
	}
}

static SDL_FRect present_rect(Platform *p) {
	int w = 0;
	int h = 0;

	SDL_GetRenderOutputSize(p->renderer, &w, &h);

	int scale = w / GB_SCREEN_W;
	if (h / GB_SCREEN_H < scale) {
		scale = h / GB_SCREEN_H;
	}

	float rw;
	float rh;

	if (scale >= 1) {
		rw = (float)(GB_SCREEN_W * scale);
		rh = (float)(GB_SCREEN_H * scale);
	} else {
		float sx = (float)w / GB_SCREEN_W;
		float sy = (float)h / GB_SCREEN_H;
		float s = sx < sy ? sx : sy;

		rw = GB_SCREEN_W * s;
		rh = GB_SCREEN_H * s;
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

	p->dialog_event = SDL_RegisterEvents(1);
	if (p->dialog_event == (Uint32)-1) {
		fprintf(stderr, "SDL_RegisterEvents: %s\n", SDL_GetError());
		goto fail;
	}

	p->window =
		SDL_CreateWindow(title, GB_SCREEN_W * scale, GB_SCREEN_H * scale, SDL_WINDOW_RESIZABLE);
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

	p->texture = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888,
								   SDL_TEXTUREACCESS_STREAMING, GB_SCREEN_W, GB_SCREEN_H);
	if (!p->texture) {
		fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
		goto fail;
	}
	SDL_SetTextureScaleMode(p->texture, SDL_SCALEMODE_NEAREST);

	p->screen = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
								  UI_VIEW_W, UI_VIEW_H);
	if (!p->screen) {
		fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
		goto fail;
	}
	SDL_SetTextureScaleMode(p->screen, SDL_SCALEMODE_NEAREST);

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
	if (p->screen) {
		SDL_DestroyTexture(p->screen);
	}
	if (p->texture) {
		SDL_DestroyTexture(p->texture);
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
			if (p->capture) {
				if (ev.key.repeat) {
					break;
				}
				out->type = ev.key.key == SDLK_ESCAPE ? PLATFORM_EV_BACK : PLATFORM_EV_KEY_CAPTURED;
				return true;
			}
			out->type = map_nav(ev.key.key);
			return true;
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

void platform_default_pad(Bindings *b) {
	memset(b->pad, 0, sizeof b->pad);
	b->pad[0][0] = SDL_SCANCODE_RIGHT;
	b->pad[1][0] = SDL_SCANCODE_LEFT;
	b->pad[2][0] = SDL_SCANCODE_UP;
	b->pad[3][0] = SDL_SCANCODE_DOWN;
	b->pad[4][0] = SDL_SCANCODE_Z;
	b->pad[5][0] = SDL_SCANCODE_X;
	b->pad[6][0] = SDL_SCANCODE_BACKSPACE;
	b->pad[6][1] = SDL_SCANCODE_RSHIFT;
	b->pad[7][0] = SDL_SCANCODE_RETURN;
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
	b->shortcut[SC_STEP_CYCLE] = SDL_SCANCODE_C;
}

const char *platform_key_name(int scancode) {
	return SDL_GetScancodeName((SDL_Scancode)scancode);
}

bool platform_key_valid(int scancode) {
	return scancode >= 0 && scancode < SDL_SCANCODE_COUNT;
}

int platform_slot_for_key(int scancode) {
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
		return scancode - SDL_SCANCODE_1 + 1;
	}
	return 0;
}

bool platform_key_reserved(int scancode) {
	return platform_slot_for_key(scancode) != 0;
}

void platform_capture_keys(Platform *p, bool on) {
	p->capture = on;
}

bool platform_system_dark(void) {
	return SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
}

void platform_show_cursor(Platform *p, bool show) {
	(void)p;
	if (show) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}
}

uint8_t platform_joypad(const Platform *p, const Bindings *bindings) {
	(void)p;
	const bool *keys = SDL_GetKeyboardState(NULL);
	uint8_t mask = 0;

	for (int b = 0; b < JOY_BUTTONS; b++) {
		for (int s = 0; s < BIND_KEY_SLOTS; s++) {
			int scancode = bindings->pad[b][s];

			if (scancode > 0 && platform_key_valid(scancode) && keys[scancode]) {
				mask |= (uint8_t)(1u << b);
			}
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

void platform_open_file_dialog(Platform *p, bool boot) {
	static const SDL_DialogFileFilter rom_filters[] = {
		{"Game Boy ROMs", "gb;gbc"},
		{"All files", "*"},
	};
	static const SDL_DialogFileFilter boot_filters[] = {
		{"Boot ROM", "bin"},
		{"All files", "*"},
	};

	SDL_ShowOpenFileDialog(on_file_dialog, (void *)(uintptr_t)p->dialog_event, p->window,
						   boot ? boot_filters : rom_filters, 2, NULL, false);
}

bool platform_make_dir(const char *path) {
	return SDL_CreateDirectory(path);
}

void platform_clear(Platform *p, uint32_t argb) {
	use_screen(p);
	set_color(p, argb);
	SDL_RenderClear(p->renderer);
}

void platform_fill_rect(Platform *p, int x, int y, int w, int h, uint32_t argb) {
	SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};

	use_screen(p);
	set_color(p, argb);
	SDL_RenderFillRect(p->renderer, &rect);
}

void platform_draw_text(Platform *p, int x, int y, int scale, uint32_t argb, const char *text) {
	use_screen(p);
	set_color(p, argb);
	SDL_SetRenderScale(p->renderer, (float)scale, (float)scale);
	SDL_RenderDebugText(p->renderer, (float)x / scale, (float)y / scale, text);
	SDL_SetRenderScale(p->renderer, 1.0f, 1.0f);
}

void platform_draw_text_centered(Platform *p, int y, int scale, uint32_t argb, const char *text) {
	int w = (int)strlen(text) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;

	platform_draw_text(p, (UI_VIEW_W - w) / 2, y, scale, argb, text);
}

void platform_draw_frame(Platform *p, const uint32_t *framebuffer) {
	use_screen(p);
	SDL_UpdateTexture(p->texture, NULL, framebuffer, GB_SCREEN_W * (int)sizeof(uint32_t));
	SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);
	SDL_RenderClear(p->renderer);
	SDL_RenderTexture(p->renderer, p->texture, NULL, NULL);
}

void platform_flip(Platform *p) {
	SDL_SetRenderTarget(p->renderer, NULL);

	SDL_FRect dst = present_rect(p);

	set_color(p, p->background);
	SDL_RenderClear(p->renderer);
	SDL_RenderTexture(p->renderer, p->screen, NULL, &dst);
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
	p->next_frame_ns += GB_FRAME_NS;
}

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
	SDL_AudioStream *audio;
	uint64_t next_frame_ns;
	Uint32 dialog_event;
	int view_w;
	int view_h;
	bool capture;
};

static void set_color(Platform *p, uint32_t argb) {
	SDL_SetRenderDrawColor(p->renderer, (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
						   (argb >> 24) & 0xFF);
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

Platform *platform_create(const char *title, int scale) {
	Platform *p = calloc(1, sizeof *p);
	if (!p) {
		return NULL;
	}

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

	p->texture = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888,
								   SDL_TEXTUREACCESS_STREAMING, GB_SCREEN_W, GB_SCREEN_H);
	if (!p->texture) {
		fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
		goto fail;
	}
	SDL_SetTextureScaleMode(p->texture, SDL_SCALEMODE_NEAREST);

	audio_open(p);
	platform_set_view(p, UI_VIEW_W, UI_VIEW_H);

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

void platform_set_view(Platform *p, int w, int h) {
	SDL_SetRenderLogicalPresentation(p->renderer, w, h, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
	p->view_w = w;
	p->view_h = h;
}

static const char *hotkey_name(SDL_Keycode key) {
	if (key >= SDLK_1 && key <= SDLK_9) {
		return "State slot";
	}

	switch (key) {
	case SDLK_P:
		return "Pause";
	case SDLK_TAB:
		return "Speed";
	case SDLK_F5:
		return "Save state";
	case SDLK_F9:
		return "Load state";
	case SDLK_N:
		return "Step frame";
	case SDLK_I:
		return "Step instruction";
	case SDLK_C:
		return "Step cycle";
	default:
		return NULL;
	}
}

static bool map_key(SDL_Keycode key, PlatformEvent *out) {
	if (key >= SDLK_1 && key <= SDLK_9) {
		out->type = PLATFORM_EV_SLOT;
		out->slot = (int)(key - SDLK_1) + 1;
		return true;
	}

	switch (key) {
	case SDLK_UP:
		out->type = PLATFORM_EV_UP;
		return true;
	case SDLK_DOWN:
		out->type = PLATFORM_EV_DOWN;
		return true;
	case SDLK_LEFT:
		out->type = PLATFORM_EV_LEFT;
		return true;
	case SDLK_RIGHT:
		out->type = PLATFORM_EV_RIGHT;
		return true;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
	case SDLK_SPACE:
		out->type = PLATFORM_EV_CONFIRM;
		return true;
	case SDLK_ESCAPE:
		out->type = PLATFORM_EV_BACK;
		return true;
	case SDLK_DELETE:
		out->type = PLATFORM_EV_CLEAR;
		return true;
	case SDLK_P:
		out->type = PLATFORM_EV_PAUSE;
		return true;
	case SDLK_TAB:
		out->type = PLATFORM_EV_SPEED;
		return true;
	case SDLK_F5:
		out->type = PLATFORM_EV_SAVE_STATE;
		return true;
	case SDLK_F9:
		out->type = PLATFORM_EV_LOAD_STATE;
		return true;
	case SDLK_N:
		out->type = PLATFORM_EV_STEP_FRAME;
		return true;
	case SDLK_I:
		out->type = PLATFORM_EV_STEP_INSTR;
		return true;
	case SDLK_C:
		out->type = PLATFORM_EV_STEP_CYCLE;
		return true;
	default:
		return false;
	}
}

static bool repeats(PlatformEventType type) {
	return type == PLATFORM_EV_STEP_FRAME || type == PLATFORM_EV_STEP_INSTR ||
		   type == PLATFORM_EV_STEP_CYCLE;
}

bool platform_next_event(Platform *p, PlatformEvent *out) {
	SDL_Event ev;

	while (SDL_PollEvent(&ev)) {
		out->path[0] = '\0';
		out->slot = 0;
		out->key = 0;
		out->hotkey = NULL;

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
		case SDL_EVENT_KEY_DOWN:
			if (p->capture) {
				if (ev.key.repeat) {
					break;
				}
				if (ev.key.key == SDLK_ESCAPE) {
					out->type = PLATFORM_EV_BACK;
					return true;
				}
				out->type = PLATFORM_EV_KEY_CAPTURED;
				out->key = (int)ev.key.scancode;
				out->hotkey = hotkey_name(ev.key.key);
				return true;
			}
			if (map_key(ev.key.key, out) && (!ev.key.repeat || repeats(out->type))) {
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

void platform_default_keymap(JoyKeymap *keymap) {
	memset(keymap, 0, sizeof *keymap);
	keymap->key[0][0] = SDL_SCANCODE_RIGHT;
	keymap->key[1][0] = SDL_SCANCODE_LEFT;
	keymap->key[2][0] = SDL_SCANCODE_UP;
	keymap->key[3][0] = SDL_SCANCODE_DOWN;
	keymap->key[4][0] = SDL_SCANCODE_Z;
	keymap->key[5][0] = SDL_SCANCODE_X;
	keymap->key[6][0] = SDL_SCANCODE_BACKSPACE;
	keymap->key[6][1] = SDL_SCANCODE_RSHIFT;
	keymap->key[7][0] = SDL_SCANCODE_RETURN;
}

const char *platform_key_name(int scancode) {
	return SDL_GetScancodeName((SDL_Scancode)scancode);
}

bool platform_key_valid(int scancode) {
	return scancode >= 0 && scancode < SDL_SCANCODE_COUNT;
}

void platform_capture_keys(Platform *p, bool on) {
	p->capture = on;
}

uint8_t platform_joypad(const Platform *p, const JoyKeymap *keymap) {
	(void)p;
	const bool *keys = SDL_GetKeyboardState(NULL);
	uint8_t mask = 0;

	for (int b = 0; b < JOY_BUTTONS; b++) {
		for (int s = 0; s < JOY_KEY_SLOTS; s++) {
			int scancode = keymap->key[b][s];

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
	set_color(p, argb);
	SDL_RenderClear(p->renderer);
}

void platform_fill_rect(Platform *p, int x, int y, int w, int h, uint32_t argb) {
	SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};

	set_color(p, argb);
	SDL_RenderFillRect(p->renderer, &rect);
}

void platform_draw_text(Platform *p, int x, int y, int scale, uint32_t argb, const char *text) {
	set_color(p, argb);
	SDL_SetRenderScale(p->renderer, (float)scale, (float)scale);
	SDL_RenderDebugText(p->renderer, (float)x / scale, (float)y / scale, text);
	SDL_SetRenderScale(p->renderer, 1.0f, 1.0f);
}

void platform_draw_text_centered(Platform *p, int y, int scale, uint32_t argb, const char *text) {
	int w = (int)strlen(text) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
	platform_draw_text(p, (p->view_w - w) / 2, y, scale, argb, text);
}

void platform_flip(Platform *p) {
	SDL_RenderPresent(p->renderer);
}

void platform_draw_frame(Platform *p, const uint32_t *framebuffer) {
	SDL_UpdateTexture(p->texture, NULL, framebuffer, GB_SCREEN_W * (int)sizeof(uint32_t));
	SDL_RenderClear(p->renderer);
	SDL_RenderTexture(p->renderer, p->texture, NULL, NULL);
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

void platform_frame_limit(Platform *p) {
	uint64_t now = SDL_GetTicksNS();
	if (p->next_frame_ns > now) {
		SDL_DelayNS(p->next_frame_ns - now);
	} else {
		p->next_frame_ns = now;
	}
	p->next_frame_ns += GB_FRAME_NS;
}

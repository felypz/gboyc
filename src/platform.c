#include "platform.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GB_FRAME_NS 16742706ULL

enum { DIALOG_CHOSEN, DIALOG_CANCELED, DIALOG_FAILED };

struct Platform {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	uint64_t next_frame_ns;
	Uint32 dialog_event;
	int view_w;
	int view_h;
};

static void set_color(Platform *p, uint32_t argb)
{
	SDL_SetRenderDrawColor(p->renderer, (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
						   (argb >> 24) & 0xFF);
}

Platform *platform_create(const char *title, int scale)
{
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

	platform_set_view(p, UI_VIEW_W, UI_VIEW_H);

	p->next_frame_ns = SDL_GetTicksNS();
	return p;

fail:
	platform_destroy(p);
	return NULL;
}

void platform_destroy(Platform *p)
{
	if (!p) {
		return;
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

void platform_set_view(Platform *p, int w, int h)
{
	SDL_SetRenderLogicalPresentation(p->renderer, w, h, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
	p->view_w = w;
	p->view_h = h;
}

static bool map_key(SDL_Keycode key, PlatformEventType *type)
{
	switch (key) {
	case SDLK_UP:
		*type = PLATFORM_EV_UP;
		return true;
	case SDLK_DOWN:
		*type = PLATFORM_EV_DOWN;
		return true;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
	case SDLK_SPACE:
		*type = PLATFORM_EV_CONFIRM;
		return true;
	case SDLK_ESCAPE:
		*type = PLATFORM_EV_BACK;
		return true;
	default:
		return false;
	}
}

bool platform_next_event(Platform *p, PlatformEvent *out)
{
	SDL_Event ev;

	while (SDL_PollEvent(&ev)) {
		out->path[0] = '\0';

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
			if (!ev.key.repeat && map_key(ev.key.key, &out->type)) {
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

static void SDLCALL on_file_dialog(void *userdata, const char *const *filelist, int filter)
{
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

void platform_open_file_dialog(Platform *p)
{
	static const SDL_DialogFileFilter filters[] = {
		{"Game Boy ROMs", "gb;gbc"},
		{"All files", "*"},
	};

	SDL_ShowOpenFileDialog(on_file_dialog, (void *)(uintptr_t)p->dialog_event, p->window, filters,
						   SDL_arraysize(filters), NULL, false);
}

void platform_clear(Platform *p, uint32_t argb)
{
	set_color(p, argb);
	SDL_RenderClear(p->renderer);
}

void platform_draw_text(Platform *p, int x, int y, int scale, uint32_t argb, const char *text)
{
	set_color(p, argb);
	SDL_SetRenderScale(p->renderer, (float)scale, (float)scale);
	SDL_RenderDebugText(p->renderer, (float)x / scale, (float)y / scale, text);
	SDL_SetRenderScale(p->renderer, 1.0f, 1.0f);
}

void platform_draw_text_centered(Platform *p, int y, int scale, uint32_t argb, const char *text)
{
	int w = (int)strlen(text) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
	platform_draw_text(p, (p->view_w - w) / 2, y, scale, argb, text);
}

void platform_flip(Platform *p)
{
	SDL_RenderPresent(p->renderer);
}

void platform_present(Platform *p, const uint32_t *framebuffer)
{
	SDL_UpdateTexture(p->texture, NULL, framebuffer, GB_SCREEN_W * (int)sizeof(uint32_t));
	SDL_RenderClear(p->renderer);
	SDL_RenderTexture(p->renderer, p->texture, NULL, NULL);
	SDL_RenderPresent(p->renderer);
}

void platform_frame_limit(Platform *p)
{
	uint64_t now = SDL_GetTicksNS();
	if (p->next_frame_ns > now) {
		SDL_DelayNS(p->next_frame_ns - now);
	} else {
		p->next_frame_ns = now;
	}
	p->next_frame_ns += GB_FRAME_NS;
}

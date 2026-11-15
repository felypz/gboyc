#include "platform.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define GB_FRAME_NS 16742706ULL

struct Platform {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	uint64_t next_frame_ns;
};

Platform *platform_create(const char *title, int scale)
{
	Platform *p = calloc(1, sizeof *p);
	if (!p)
		return NULL;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
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

	SDL_SetRenderLogicalPresentation(p->renderer, GB_SCREEN_W, GB_SCREEN_H,
									 SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

	p->texture = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_ARGB8888,
								   SDL_TEXTUREACCESS_STREAMING, GB_SCREEN_W, GB_SCREEN_H);
	if (!p->texture) {
		fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
		goto fail;
	}
	SDL_SetTextureScaleMode(p->texture, SDL_SCALEMODE_NEAREST);

	p->next_frame_ns = SDL_GetTicksNS();
	return p;

fail:
	platform_destroy(p);
	return NULL;
}

void platform_destroy(Platform *p)
{
	if (!p)
		return;
	if (p->texture)
		SDL_DestroyTexture(p->texture);
	if (p->renderer)
		SDL_DestroyRenderer(p->renderer);
	if (p->window)
		SDL_DestroyWindow(p->window);
	SDL_Quit();
	free(p);
}

bool platform_poll_events(Platform *p)
{
	(void)p;
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_EVENT_QUIT:
			return false;
		case SDL_EVENT_KEY_DOWN:
			if (ev.key.key == SDLK_ESCAPE)
				return false;
			break;
		}
	}
	return true;
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
	if (p->next_frame_ns > now)
		SDL_DelayNS(p->next_frame_ns - now);
	else
		p->next_frame_ns = now;
	p->next_frame_ns += GB_FRAME_NS;
}

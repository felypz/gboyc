#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#define GB_SCREEN_W 160
#define GB_SCREEN_H 144

typedef struct Platform Platform;

Platform *platform_create(const char *title, int scale);
void platform_destroy(Platform *p);

bool platform_poll_events(Platform *p);

void platform_present(Platform *p, const uint32_t *framebuffer);

void platform_frame_limit(Platform *p);

#endif

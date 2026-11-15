#include <stdio.h>
#include <stdlib.h>

#include "platform.h"

static const uint32_t DMG_PALETTE[4] = {
	0xFF9BBC0F,
	0xFF8BAC0F,
	0xFF306230,
	0xFF0F380F,
};

static uint32_t framebuffer[GB_SCREEN_W * GB_SCREEN_H];

static void draw_test_pattern(void)
{
	for (int y = 0; y < GB_SCREEN_H; y++)
		for (int x = 0; x < GB_SCREEN_W; x++)
			framebuffer[y * GB_SCREEN_W + x] = DMG_PALETTE[x / (GB_SCREEN_W / 4)];
}

int main(void)
{
	Platform *plat = platform_create("gbemu", 4);
	if (!plat)
		return EXIT_FAILURE;

	draw_test_pattern();

	while (platform_poll_events(plat)) {
		platform_present(plat, framebuffer);
		platform_frame_limit(plat);
	}

	platform_destroy(plat);
	return EXIT_SUCCESS;
}

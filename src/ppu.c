#include "ppu.h"

#include <string.h>

#include "memmap.h"
#include "palette.h"
#include "timing.h"

#define DOTS_PER_LINE		 456
#define LINES_PER_FRAME		 154
#define OAM_SCAN_DOTS		 80
#define MODE3_STARTUP_DOTS	 12
#define WINDOW_STALL_DOTS	 6
#define LCD_ON_START_DOT	 4
#define OAM_ENTRIES			 40
#define OAM_ENTRY_BYTES		 4
#define MAX_SPRITES_PER_LINE 10

_Static_assert(DOTS_PER_LINE *LINES_PER_FRAME == GB_DOTS_PER_FRAME, "frame timing mismatch");

#define LCDC_ENABLE		0x80
#define LCDC_WIN_MAP	0x40
#define LCDC_WIN_ENABLE 0x20
#define LCDC_TILE_DATA	0x10
#define LCDC_BG_MAP		0x08
#define LCDC_OBJ_TALL	0x04
#define LCDC_OBJ_ENABLE 0x02
#define LCDC_BG_ENABLE	0x01

#define STAT_INT_HBLANK 0x08
#define STAT_INT_VBLANK 0x10
#define STAT_INT_OAM	0x20
#define STAT_INT_LYC	0x40
#define STAT_WRITABLE	0x78
#define STAT_LYC_MATCH	0x04
#define STAT_UNUSED_BIT 0x80

#define OBJ_BEHIND_BG 0x80
#define OBJ_FLIP_Y	  0x40
#define OBJ_FLIP_X	  0x20
#define OBJ_PALETTE1  0x10

/* Sprite OAM coordinates are offset so that 0 means "just off-screen". */
#define OBJ_Y_OFFSET 16
#define OBJ_X_OFFSET 8
#define OBJ_X_MAX	 168 /* sprites at X >= 168 never appear */
#define WX_OFFSET	 7
#define WX_MAX		 166 /* the window cannot start beyond this WX */

/* Fetch penalty of a sprite: 11 dots minus how far into the tile the sprite starts (max 5). */
#define SPRITE_STALL_BASE		11
#define SPRITE_STALL_MAX_OFFSET 5

/* VRAM layout (offsets from 0x8000). */
#define TILE_BYTES		   16
#define TILE_ROW_BYTES	   2
#define TILE_BLOCK2_OFFSET 0x1000 /* signed tile ids index from here when LCDC.4 is clear */
#define MAP0_OFFSET		   0x1800
#define MAP1_OFFSET		   0x1C00
#define MAP_TILES_PER_ROW  32

/* Boot ROM leaves the LCD on with the standard greyscale palette. */
#define POST_BOOT_LCDC 0x91
#define POST_BOOT_BGP  0xFC

/* Mode 0/1/2 raise STAT interrupts; mode 3 does not. */
#define FIRST_STAT_IRQ_MODE PPU_MODE_OAM_SCAN

#define OAM_ROW_BYTES 8
#define OAM_ROW_DOTS  4
/* The read-with-increment OAM bug only corrupts these rows. */
#define OAM_IDU_FIRST_ROW 4
#define OAM_IDU_LAST_ROW  18

_Static_assert(sizeof(((Ppu *)0)->vram) == MEM_VRAM_SIZE, "VRAM size mismatch");
_Static_assert(sizeof(((Ppu *)0)->oam) == MEM_OAM_SIZE, "OAM size mismatch");

static const uint32_t SHADES[4] = {DMG_COLOR_0, DMG_COLOR_1, DMG_COLOR_2, DMG_COLOR_3};

static uint32_t shade(uint8_t palette, unsigned id) {
	return SHADES[(palette >> (id * 2)) & 3];
}

static bool lcd_on(const Ppu *p) {
	return p->lcdc & LCDC_ENABLE;
}

static int obj_height(const Ppu *p) {
	return (p->lcdc & LCDC_OBJ_TALL) ? 16 : 8;
}

static void clear_screen(Ppu *p) {
	for (unsigned i = 0; i < PPU_W * PPU_H; i++) {
		p->framebuffer[i] = SHADES[0];
	}
}

/* Color id (0-3) of one pixel in a tile row; `bit` 7 is the leftmost pixel. */
static unsigned tile_row_pixel(const uint8_t *vram, unsigned row_addr, unsigned bit) {
	return (((vram[row_addr + 1] >> bit) & 1) << 1) | ((vram[row_addr] >> bit) & 1);
}

static unsigned scan_sprites(const Ppu *p, uint8_t *found) {
	int height = obj_height(p);
	unsigned count = 0;

	for (unsigned i = 0; i < OAM_ENTRIES && count < MAX_SPRITES_PER_LINE; i++) {
		int line = p->ly - (p->oam[i * OAM_ENTRY_BYTES] - OBJ_Y_OFFSET);

		if (line >= 0 && line < height) {
			found[count++] = (uint8_t)i;
		}
	}
	return count;
}

/* DMG priority: lower X wins, ties go to the lower OAM index (stable insertion sort). */
static void sort_sprites_by_x(const Ppu *p, uint8_t *found, unsigned count) {
	for (unsigned i = 1; i < count; i++) {
		uint8_t cur = found[i];
		unsigned j = i;

		while (j > 0 &&
			   p->oam[found[j - 1] * OAM_ENTRY_BYTES + 1] > p->oam[cur * OAM_ENTRY_BYTES + 1]) {
			found[j] = found[j - 1];
			j--;
		}
		found[j] = cur;
	}
}

/* Background/window pixel at (x, y) of the 256x256 map starting at `map`. */
static unsigned tile_pixel(const Ppu *p, unsigned map, unsigned x, unsigned y) {
	x &= 0xFF;
	y &= 0xFF;

	uint8_t tile = p->vram[map + (y >> 3) * MAP_TILES_PER_ROW + (x >> 3)];
	unsigned addr = (p->lcdc & LCDC_TILE_DATA)
						? tile * TILE_BYTES
						: (unsigned)(TILE_BLOCK2_OFFSET + (int8_t)tile * TILE_BYTES);

	addr += (y & 7) * TILE_ROW_BYTES;
	return tile_row_pixel(p->vram, addr, 7 - (x & 7));
}

static unsigned background_id(const Ppu *p) {
	if (!(p->lcdc & LCDC_BG_ENABLE)) {
		return 0;
	}

	if (p->win_on) {
		unsigned map = (p->lcdc & LCDC_WIN_MAP) ? MAP1_OFFSET : MAP0_OFFSET;
		return tile_pixel(p, map, (unsigned)(p->lcd_x - p->win_start), p->window_line);
	}

	unsigned map = (p->lcdc & LCDC_BG_MAP) ? MAP1_OFFSET : MAP0_OFFSET;
	unsigned x = (unsigned)(p->scx & 0xF8) + p->lcd_x + p->scx_fine;
	unsigned y = (unsigned)p->ly + p->scy;

	return tile_pixel(p, map, x, y);
}

static void sprite_pixel(const Ppu *p, unsigned bg_id, uint32_t *color) {
	if (!(p->lcdc & LCDC_OBJ_ENABLE)) {
		return;
	}

	int height = obj_height(p);

	for (unsigned s = 0; s < p->sprite_count; s++) {
		const uint8_t *obj = &p->oam[p->sprites[s] * OAM_ENTRY_BYTES];
		int px = (int)p->lcd_x + OBJ_X_OFFSET - obj[1];

		if (px < 0 || px > 7) {
			continue;
		}

		unsigned attr = obj[3];
		int line = p->ly - (obj[0] - OBJ_Y_OFFSET);

		if (line < 0 || line >= height) {
			continue;
		}
		unsigned tile = obj[2];

		if (attr & OBJ_FLIP_Y) {
			line = height - 1 - line;
		}
		if (height == 16) {
			tile &= 0xFE;
		}

		unsigned addr = tile * TILE_BYTES + (unsigned)line * TILE_ROW_BYTES;
		unsigned bit = (attr & OBJ_FLIP_X) ? (unsigned)px : (unsigned)(7 - px);
		unsigned id = tile_row_pixel(p->vram, addr, bit);

		if (id == 0) {
			continue; /* transparent: a lower-priority sprite may show through */
		}
		if ((attr & OBJ_BEHIND_BG) && bg_id != 0) {
			return;
		}
		*color = shade((attr & OBJ_PALETTE1) ? p->obp1 : p->obp0, id);
		return;
	}
}

static void output_pixel(Ppu *p) {
	unsigned id = background_id(p);
	uint32_t color = (p->lcdc & LCDC_BG_ENABLE) ? shade(p->bgp, id) : SHADES[0];

	sprite_pixel(p, id, &color);
	p->framebuffer[p->ly * PPU_W + p->lcd_x] = color;
}

static void start_drawing(Ppu *p) {
	uint8_t found[MAX_SPRITES_PER_LINE];
	unsigned count = scan_sprites(p, found);

	sort_sprites_by_x(p, found, count);
	memcpy(p->sprites, found, count);
	p->sprite_count = (uint8_t)count;
	p->sprite_next = 0;

	if ((p->lcdc & LCDC_WIN_ENABLE) && p->ly == p->wy) {
		p->wy_trigger = true;
	}

	p->drawing = true;
	p->lcd_x = 0;
	p->win_on = false;
	p->win_used = false;
	p->scx_fine = p->scx & 7;
	p->stall = (uint8_t)(MODE3_STARTUP_DOTS + p->scx_fine);
}

static bool window_starts(const Ppu *p) {
	if (p->win_on || !p->wy_trigger || !(p->lcdc & LCDC_WIN_ENABLE) ||
		!(p->lcdc & LCDC_BG_ENABLE) || p->wx > WX_MAX) {
		return false;
	}
	return p->lcd_x + WX_OFFSET == p->wx || (p->lcd_x == 0 && p->wx < WX_OFFSET);
}

static unsigned sprite_stall(const Ppu *p, unsigned x) {
	unsigned offset = (x + p->scx) & 7;

	return SPRITE_STALL_BASE -
		   (offset < SPRITE_STALL_MAX_OFFSET ? offset : SPRITE_STALL_MAX_OFFSET);
}

/* One dot of mode 3 (pixel transfer): fetch stalls first, then at most one pixel is output. */
static void mode3_dot(Ppu *p) {
	if (p->stall) {
		p->stall--;
		return;
	}

	if (window_starts(p)) {
		p->win_on = true;
		p->win_used = true;
		p->win_start = (int16_t)(p->wx - WX_OFFSET);
		p->stall += WINDOW_STALL_DOTS;
	}

	while (p->sprite_next < p->sprite_count) {
		unsigned x = p->oam[p->sprites[p->sprite_next] * OAM_ENTRY_BYTES + 1];
		unsigned trigger = x < OBJ_X_OFFSET ? 0 : x - OBJ_X_OFFSET;

		if (x >= OBJ_X_MAX || trigger > p->lcd_x) {
			break;
		}
		if (p->lcdc & LCDC_OBJ_ENABLE) {
			p->stall += (uint8_t)sprite_stall(p, x);
		}
		p->sprite_next++;
	}

	if (p->stall) {
		p->stall--;
		return;
	}

	if (p->win_on && !(p->lcdc & LCDC_WIN_ENABLE)) {
		p->win_on = false;
	}

	output_pixel(p);

	if (++p->lcd_x == PPU_W) {
		p->drawing = false;
		if (p->win_used) {
			p->window_line++;
		}
	}
}

static bool stat_line_now(const Ppu *p) {
	return lcd_on(p) && (((p->stat & STAT_INT_HBLANK) && p->mode == PPU_MODE_HBLANK) ||
						 ((p->stat & STAT_INT_VBLANK) && p->mode == PPU_MODE_VBLANK) ||
						 ((p->stat & STAT_INT_OAM) && p->mode == PPU_MODE_OAM_SCAN) ||
						 ((p->stat & STAT_INT_LYC) && p->ly == p->lyc));
}

/* STAT interrupts fire on the rising edge of the OR of all enabled sources. */
static bool stat_edge(Ppu *p) {
	bool line = stat_line_now(p);
	bool rising = line && !p->stat_line;

	p->stat_line = line;
	return rising;
}

static unsigned dot_step(Ppu *p) {
	unsigned irq = 0;

	if (++p->dot >= DOTS_PER_LINE) {
		p->dot = 0;
		p->first_line = false;
		/*
		 * WY is sampled twice per line: here, as the line ends, and again when mode 3
		 * starts (start_drawing). The second sample catches a WY change made during
		 * the line; this one keeps the trigger latched for the lines that follow.
		 */
		if (p->ly < PPU_H && (p->lcdc & LCDC_WIN_ENABLE) && p->ly == p->wy) {
			p->wy_trigger = true;
		}
		if (++p->ly == PPU_H) {
			irq |= PPU_IRQ_VBLANK;
			p->frame_done = true;
		} else if (p->ly == LINES_PER_FRAME) {
			p->ly = 0;
			p->window_line = 0;
			p->wy_trigger = false;
		}
	}

	if (p->ly < PPU_H && p->dot == OAM_SCAN_DOTS) {
		start_drawing(p);
	}

	if (p->ly >= PPU_H) {
		p->mode = PPU_MODE_VBLANK;
	} else if (p->dot < OAM_SCAN_DOTS) {
		/* The first line after turning the LCD on skips OAM scan and reports mode 0. */
		p->mode = p->first_line ? PPU_MODE_HBLANK : PPU_MODE_OAM_SCAN;
	} else {
		p->mode = p->drawing ? PPU_MODE_DRAWING : PPU_MODE_HBLANK;
	}

	if (p->mode == PPU_MODE_DRAWING) {
		mode3_dot(p);
	}
	return irq;
}

void ppu_reset(Ppu *p, bool has_boot_rom) {
	memset(p, 0, sizeof *p);
	p->lcdc = has_boot_rom ? 0 : POST_BOOT_LCDC;
	p->bgp = has_boot_rom ? 0 : POST_BOOT_BGP;
	p->obp0 = 0xFF;
	p->obp1 = 0xFF;
	p->mode = PPU_MODE_HBLANK;
	clear_screen(p);
}

unsigned ppu_mcycle(Ppu *p) {
	if (!lcd_on(p)) {
		/* With the LCD off no frames are drawn, but a frame is still signalled at 60 Hz. */
		p->off_dots += GB_DOTS_PER_MCYCLE;
		if (p->off_dots >= GB_DOTS_PER_FRAME) {
			p->off_dots -= GB_DOTS_PER_FRAME;
			p->frame_done = true;
		}
		return 0;
	}

	unsigned irq = 0;

	for (unsigned i = 0; i < GB_DOTS_PER_MCYCLE; i++) {
		irq |= dot_step(p);
	}
	if (stat_edge(p)) {
		irq |= PPU_IRQ_STAT;
	}
	return irq;
}

bool ppu_take_frame(Ppu *p) {
	bool done = p->frame_done;

	p->frame_done = false;
	return done;
}

bool ppu_state_sane(const Ppu *p) {
	if (p->mode > PPU_MODE_DRAWING || p->ly >= LINES_PER_FRAME || p->dot >= DOTS_PER_LINE ||
		p->lcd_x > PPU_W || p->sprite_count > MAX_SPRITES_PER_LINE ||
		p->sprite_next > p->sprite_count) {
		return false;
	}
	for (unsigned i = 0; i < p->sprite_count; i++) {
		if (p->sprites[i] >= OAM_ENTRIES) {
			return false;
		}
	}
	return true;
}

uint8_t ppu_read(const Ppu *p, uint16_t addr) {
	switch (addr) {
	case REG_LCDC:
		return p->lcdc;
	case REG_STAT:
		return (uint8_t)(STAT_UNUSED_BIT | p->stat | (p->ly == p->lyc ? STAT_LYC_MATCH : 0) |
						 p->mode);
	case REG_SCY:
		return p->scy;
	case REG_SCX:
		return p->scx;
	case REG_LY:
		return p->ly;
	case REG_LYC:
		return p->lyc;
	case REG_BGP:
		return p->bgp;
	case REG_OBP0:
		return p->obp0;
	case REG_OBP1:
		return p->obp1;
	case REG_WY:
		return p->wy;
	case REG_WX:
		return p->wx;
	default:
		return 0xFF;
	}
}

static void lcdc_write(Ppu *p, uint8_t value) {
	bool was_on = lcd_on(p);

	p->lcdc = value;
	if (was_on == lcd_on(p)) {
		return;
	}

	/* Switching the LCD on or off restarts the frame. */
	p->ly = 0;
	p->dot = lcd_on(p) ? LCD_ON_START_DOT : 0;
	p->mode = PPU_MODE_HBLANK;
	p->window_line = 0;
	p->off_dots = 0;
	p->first_line = lcd_on(p);
	p->drawing = false;
	p->wy_trigger = false;
	p->win_on = false;
	p->win_used = false;
	p->lcd_x = 0;
	p->stall = 0;
	if (!lcd_on(p)) {
		clear_screen(p);
	}
}

unsigned ppu_write(Ppu *p, uint16_t addr, uint8_t value) {
	bool spurious = false;

	switch (addr) {
	case REG_LCDC:
		lcdc_write(p, value);
		break;
	case REG_STAT:
		/* DMG quirk: writing STAT can raise a spurious interrupt in modes 0/1 or on LYC match. */
		spurious = lcd_on(p) && !p->stat_line && (p->mode < FIRST_STAT_IRQ_MODE || p->ly == p->lyc);
		p->stat = value & STAT_WRITABLE;
		break;
	case REG_SCY:
		p->scy = value;
		break;
	case REG_SCX:
		p->scx = value;
		break;
	case REG_LYC:
		p->lyc = value;
		break;
	case REG_BGP:
		p->bgp = value;
		break;
	case REG_OBP0:
		p->obp0 = value;
		break;
	case REG_OBP1:
		p->obp1 = value;
		break;
	case REG_WY:
		p->wy = value;
		break;
	case REG_WX:
		p->wx = value;
		break;
	default:
		break;
	}
	return (stat_edge(p) || spurious) ? PPU_IRQ_STAT : 0;
}

/* VRAM is locked during pixel transfer, OAM during OAM scan and pixel transfer. */
static bool vram_open(const Ppu *p) {
	return !lcd_on(p) || p->mode != PPU_MODE_DRAWING;
}

static bool oam_open(const Ppu *p) {
	return !lcd_on(p) || p->mode == PPU_MODE_HBLANK || p->mode == PPU_MODE_VBLANK;
}

uint8_t ppu_read_vram(const Ppu *p, uint16_t offset) {
	return vram_open(p) ? p->vram[offset & (MEM_VRAM_SIZE - 1)] : 0xFF;
}

void ppu_write_vram(Ppu *p, uint16_t offset, uint8_t value) {
	if (vram_open(p)) {
		p->vram[offset & (MEM_VRAM_SIZE - 1)] = value;
	}
}

uint8_t ppu_read_oam(const Ppu *p, uint16_t offset) {
	return oam_open(p) ? p->oam[offset] : 0xFF;
}

void ppu_write_oam(Ppu *p, uint16_t offset, uint8_t value) {
	if (oam_open(p)) {
		p->oam[offset] = value;
	}
}

/*
 * OAM corruption bug (DMG): touching 0xFE00-0xFEFF, or putting a 16-bit register
 * pair on the address bus, during mode 2 corrupts the OAM row being scanned.
 * A row is 8 bytes (4 little-endian words).
 */
static uint16_t oam_word(const Ppu *p, unsigned row, unsigned i) {
	return (uint16_t)(p->oam[row * OAM_ROW_BYTES + i * 2] |
					  (p->oam[row * OAM_ROW_BYTES + i * 2 + 1] << 8));
}

static void oam_set_word(Ppu *p, unsigned row, unsigned i, uint16_t value) {
	p->oam[row * OAM_ROW_BYTES + i * 2] = (uint8_t)(value & 0xFF);
	p->oam[row * OAM_ROW_BYTES + i * 2 + 1] = (uint8_t)(value >> 8);
}

/* Words 1-3 of a corrupted row are copied from the previous row. */
static void oam_copy_tail(Ppu *p, unsigned row) {
	memcpy(&p->oam[row * OAM_ROW_BYTES + 2], &p->oam[(row - 1) * OAM_ROW_BYTES + 2],
		   OAM_ROW_BYTES - 2);
}

static void oam_corrupt_write(Ppu *p, unsigned row) {
	if (row == 0) {
		return;
	}

	uint16_t a = oam_word(p, row, 0);
	uint16_t b = oam_word(p, row - 1, 0);
	uint16_t c = oam_word(p, row - 1, 2);

	oam_set_word(p, row, 0, (uint16_t)(((a ^ c) & (b ^ c)) ^ c));
	oam_copy_tail(p, row);
}

static void oam_corrupt_read(Ppu *p, unsigned row) {
	if (row == 0) {
		return;
	}

	uint16_t a = oam_word(p, row, 0);
	uint16_t b = oam_word(p, row - 1, 0);
	uint16_t c = oam_word(p, row - 1, 2);

	oam_set_word(p, row, 0, (uint16_t)(b | (a & c)));
	oam_copy_tail(p, row);
}

static void oam_corrupt_read_idu(Ppu *p, unsigned row) {
	if (row >= OAM_IDU_FIRST_ROW && row <= OAM_IDU_LAST_ROW) {
		uint16_t a = oam_word(p, row - 2, 0);
		uint16_t b = oam_word(p, row - 1, 0);
		uint16_t c = oam_word(p, row, 0);
		uint16_t d = oam_word(p, row - 1, 2);

		oam_set_word(p, row - 1, 0, (uint16_t)((b & (a | c | d)) | (a & c & d)));
		memcpy(&p->oam[row * OAM_ROW_BYTES], &p->oam[(row - 1) * OAM_ROW_BYTES], OAM_ROW_BYTES);
		memcpy(&p->oam[(row - 2) * OAM_ROW_BYTES], &p->oam[(row - 1) * OAM_ROW_BYTES],
			   OAM_ROW_BYTES);
	}
	oam_corrupt_read(p, row);
}

void ppu_oam_bug(Ppu *p, OamBug kind) {
	if (!lcd_on(p) || p->mode != PPU_MODE_OAM_SCAN) {
		return;
	}

	unsigned row = p->dot / OAM_ROW_DOTS;

	switch (kind) {
	case OAM_BUG_WRITE:
		oam_corrupt_write(p, row);
		break;
	case OAM_BUG_READ:
		oam_corrupt_read(p, row);
		break;
	case OAM_BUG_READ_IDU:
		oam_corrupt_read_idu(p, row);
		break;
	}
}

#include "ppu.h"

#include <string.h>

#include "palette.h"

#define DOTS_PER_LINE		 456
#define LINES_PER_FRAME		 154
#define DOTS_PER_FRAME		 (DOTS_PER_LINE * LINES_PER_FRAME)
#define OAM_SCAN_DOTS		 80
#define MODE3_STARTUP_DOTS	 12
#define WINDOW_STALL_DOTS	 6
#define LCD_ON_START_DOT	 4
#define OAM_ENTRIES			 40
#define MAX_SPRITES_PER_LINE 10

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

#define OBJ_BEHIND_BG 0x80
#define OBJ_FLIP_Y	  0x40
#define OBJ_FLIP_X	  0x20
#define OBJ_PALETTE1  0x10

#define REG_LCDC 0xFF40
#define REG_STAT 0xFF41
#define REG_SCY	 0xFF42
#define REG_SCX	 0xFF43
#define REG_LY	 0xFF44
#define REG_LYC	 0xFF45
#define REG_BGP	 0xFF47
#define REG_OBP0 0xFF48
#define REG_OBP1 0xFF49
#define REG_WY	 0xFF4A
#define REG_WX	 0xFF4B

enum {
	MODE_HBLANK,
	MODE_VBLANK,
	MODE_OAM,
	MODE_DRAW,
};

static const uint32_t SHADES[4] = {DMG_COLOR_0, DMG_COLOR_1, DMG_COLOR_2, DMG_COLOR_3};

static uint32_t shade(uint8_t palette, unsigned id) {
	return SHADES[(palette >> (id * 2)) & 3];
}

static bool lcd_on(const Ppu *p) {
	return p->lcdc & LCDC_ENABLE;
}

static void clear_screen(Ppu *p) {
	for (unsigned i = 0; i < PPU_W * PPU_H; i++) {
		p->framebuffer[i] = SHADES[0];
	}
}

static unsigned scan_sprites(const Ppu *p, uint8_t *found) {
	int height = (p->lcdc & LCDC_OBJ_TALL) ? 16 : 8;
	unsigned count = 0;

	for (unsigned i = 0; i < OAM_ENTRIES && count < MAX_SPRITES_PER_LINE; i++) {
		int line = p->ly - (p->oam[i * 4] - 16);
		if (line >= 0 && line < height) {
			found[count++] = (uint8_t)i;
		}
	}
	return count;
}

static unsigned tile_pixel(const Ppu *p, unsigned map, unsigned x, unsigned y) {
	x &= 0xFF;
	y &= 0xFF;

	uint8_t tile = p->vram[map + (y >> 3) * 32 + (x >> 3)];
	unsigned addr =
		(p->lcdc & LCDC_TILE_DATA) ? tile * 16u : (unsigned)(0x1000 + (int8_t)tile * 16);
	unsigned bit = 7 - (x & 7);

	addr += (y & 7) * 2;
	return (((p->vram[addr + 1] >> bit) & 1) << 1) | ((p->vram[addr] >> bit) & 1);
}

static unsigned background_id(const Ppu *p) {
	if (!(p->lcdc & LCDC_BG_ENABLE)) {
		return 0;
	}

	if (p->win_on) {
		unsigned map = (p->lcdc & LCDC_WIN_MAP) ? 0x1C00 : 0x1800;
		return tile_pixel(p, map, (unsigned)(p->lcd_x - p->win_start), p->window_line);
	}

	unsigned map = (p->lcdc & LCDC_BG_MAP) ? 0x1C00 : 0x1800;
	unsigned x = (unsigned)(p->scx & 0xF8) + p->lcd_x + p->scx_fine;
	unsigned y = (unsigned)p->ly + p->scy;

	return tile_pixel(p, map, x, y);
}

static void sprite_pixel(const Ppu *p, unsigned bg_id, uint32_t *color) {
	if (!(p->lcdc & LCDC_OBJ_ENABLE)) {
		return;
	}

	int height = (p->lcdc & LCDC_OBJ_TALL) ? 16 : 8;

	for (unsigned s = 0; s < p->sprite_count; s++) {
		const uint8_t *obj = &p->oam[p->sprites[s] * 4];
		int px = (int)p->lcd_x + 8 - obj[1];

		if (px < 0 || px > 7) {
			continue;
		}

		unsigned attr = obj[3];
		int line = p->ly - (obj[0] - 16);
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

		unsigned addr = tile * 16 + (unsigned)line * 2;
		unsigned bit = (attr & OBJ_FLIP_X) ? (unsigned)px : (unsigned)(7 - px);
		unsigned id = (((p->vram[addr + 1] >> bit) & 1) << 1) | ((p->vram[addr] >> bit) & 1);

		if (id == 0) {
			continue;
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

	for (unsigned i = 1; i < count; i++) {
		uint8_t cur = found[i];
		unsigned j = i;
		while (j > 0 && p->oam[found[j - 1] * 4 + 1] > p->oam[cur * 4 + 1]) {
			found[j] = found[j - 1];
			j--;
		}
		found[j] = cur;
	}

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
		!(p->lcdc & LCDC_BG_ENABLE) || p->wx > 166) {
		return false;
	}
	return p->lcd_x + 7 == p->wx || (p->lcd_x == 0 && p->wx < 7);
}

static unsigned sprite_stall(const Ppu *p, unsigned x) {
	unsigned off = (x + p->scx) & 7;
	return 11 - (off < 5 ? off : 5);
}

static void mode3_dot(Ppu *p) {
	if (p->stall) {
		p->stall--;
		return;
	}

	if (window_starts(p)) {
		p->win_on = true;
		p->win_used = true;
		p->win_start = (int16_t)(p->wx - 7);
		p->stall += WINDOW_STALL_DOTS;
	}

	while (p->sprite_next < p->sprite_count) {
		unsigned x = p->oam[p->sprites[p->sprite_next] * 4 + 1];
		unsigned trigger = x < 8 ? 0 : x - 8;

		if (x >= 168 || trigger > p->lcd_x) {
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
	return lcd_on(p) && (((p->stat & STAT_INT_HBLANK) && p->mode == MODE_HBLANK) ||
						 ((p->stat & STAT_INT_VBLANK) && p->mode == MODE_VBLANK) ||
						 ((p->stat & STAT_INT_OAM) && p->mode == MODE_OAM) ||
						 ((p->stat & STAT_INT_LYC) && p->ly == p->lyc));
}

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
		p->mode = MODE_VBLANK;
	} else if (p->dot < OAM_SCAN_DOTS) {
		p->mode = p->first_line ? MODE_HBLANK : MODE_OAM;
	} else {
		p->mode = p->drawing ? MODE_DRAW : MODE_HBLANK;
	}

	if (p->mode == MODE_DRAW) {
		mode3_dot(p);
	}
	return irq;
}

static unsigned advance(Ppu *p) {
	unsigned irq = 0;

	for (int i = 0; i < 4; i++) {
		irq |= dot_step(p);
	}
	if (stat_edge(p)) {
		irq |= PPU_IRQ_STAT;
	}
	return irq;
}

void ppu_reset(Ppu *p) {
	memset(p, 0, sizeof *p);
	p->lcdc = 0x91;
	p->bgp = 0xFC;
	p->obp0 = 0xFF;
	p->obp1 = 0xFF;
	p->mode = MODE_HBLANK;
	clear_screen(p);
}

unsigned ppu_step(Ppu *p, int cycles) {
	unsigned irq = 0;

	if (!lcd_on(p)) {
		p->off_dots += (uint32_t)cycles;
		if (p->off_dots >= DOTS_PER_FRAME) {
			p->off_dots -= DOTS_PER_FRAME;
			p->frame_done = true;
		}
		return 0;
	}

	for (int i = 0; i < cycles; i += 4) {
		irq |= advance(p);
	}
	return irq;
}

bool ppu_take_frame(Ppu *p) {
	bool done = p->frame_done;
	p->frame_done = false;
	return done;
}

uint8_t ppu_read(const Ppu *p, uint16_t addr) {
	switch (addr) {
	case REG_LCDC:
		return p->lcdc;
	case REG_STAT:
		return (uint8_t)(0x80 | p->stat | (p->ly == p->lyc ? 0x04 : 0) | p->mode);
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

	p->ly = 0;
	p->dot = lcd_on(p) ? LCD_ON_START_DOT : 0;
	p->mode = MODE_HBLANK;
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
		spurious = lcd_on(p) && !p->stat_line && (p->mode < MODE_OAM || p->ly == p->lyc);
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

static bool vram_open(const Ppu *p) {
	return !lcd_on(p) || p->mode != MODE_DRAW;
}

static bool oam_open(const Ppu *p) {
	return !lcd_on(p) || p->mode == MODE_HBLANK || p->mode == MODE_VBLANK;
}

uint8_t ppu_read_vram(const Ppu *p, uint16_t addr) {
	return vram_open(p) ? p->vram[addr & 0x1FFF] : 0xFF;
}

void ppu_write_vram(Ppu *p, uint16_t addr, uint8_t value) {
	if (vram_open(p)) {
		p->vram[addr & 0x1FFF] = value;
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

static uint16_t oam_word(const Ppu *p, unsigned row, unsigned i) {
	return (uint16_t)(p->oam[row * 8 + i * 2] | (p->oam[row * 8 + i * 2 + 1] << 8));
}

static void oam_set_word(Ppu *p, unsigned row, unsigned i, uint16_t value) {
	p->oam[row * 8 + i * 2] = (uint8_t)(value & 0xFF);
	p->oam[row * 8 + i * 2 + 1] = (uint8_t)(value >> 8);
}

static void oam_copy_tail(Ppu *p, unsigned row) {
	memcpy(&p->oam[row * 8 + 2], &p->oam[(row - 1) * 8 + 2], 6);
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
	if (row >= 4 && row <= 18) {
		uint16_t a = oam_word(p, row - 2, 0);
		uint16_t b = oam_word(p, row - 1, 0);
		uint16_t c = oam_word(p, row, 0);
		uint16_t d = oam_word(p, row - 1, 2);

		oam_set_word(p, row - 1, 0, (uint16_t)((b & (a | c | d)) | (a & c & d)));
		memcpy(&p->oam[row * 8], &p->oam[(row - 1) * 8], 8);
		memcpy(&p->oam[(row - 2) * 8], &p->oam[(row - 1) * 8], 8);
	}
	oam_corrupt_read(p, row);
}

void ppu_oam_bug(Ppu *p, OamBug kind) {
	if (!lcd_on(p) || p->mode != MODE_OAM) {
		return;
	}

	unsigned row = p->dot / 4;

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

#ifndef GBOYC_PPU_H
#define GBOYC_PPU_H

#include <stdbool.h>
#include <stdint.h>

#define PPU_W 160
#define PPU_H 144

#define PPU_IRQ_VBLANK 0x01
#define PPU_IRQ_STAT   0x02

typedef struct {
	uint8_t vram[0x2000];
	uint8_t oam[0xA0];
	uint8_t lcdc;
	uint8_t stat;
	uint8_t scy;
	uint8_t scx;
	uint8_t ly;
	uint8_t lyc;
	uint8_t bgp;
	uint8_t obp0;
	uint8_t obp1;
	uint8_t wy;
	uint8_t wx;
	uint8_t mode;
	uint8_t window_line;
	uint16_t dot;
	uint32_t off_dots;
	bool stat_line;
	bool frame_done;
	bool first_line;
	bool drawing;
	bool wy_trigger;
	bool win_on;
	bool win_used;
	uint8_t lcd_x;
	uint8_t scx_fine;
	uint8_t stall;
	int16_t win_start;
	uint8_t sprite_count;
	uint8_t sprite_next;
	uint8_t sprites[10];
	uint32_t framebuffer[PPU_W * PPU_H];
} Ppu;

typedef enum {
	OAM_BUG_READ,
	OAM_BUG_WRITE,
	OAM_BUG_READ_IDU,
} OamBug;

void ppu_oam_bug(Ppu *p, OamBug kind);

void ppu_reset(Ppu *p);
unsigned ppu_step(Ppu *p, int cycles);
bool ppu_take_frame(Ppu *p);

uint8_t ppu_read(const Ppu *p, uint16_t addr);
unsigned ppu_write(Ppu *p, uint16_t addr, uint8_t value);

uint8_t ppu_read_vram(const Ppu *p, uint16_t addr);
void ppu_write_vram(Ppu *p, uint16_t addr, uint8_t value);
uint8_t ppu_read_oam(const Ppu *p, uint16_t offset);
void ppu_write_oam(Ppu *p, uint16_t offset, uint8_t value);

#endif

#ifndef GBOYC_MAPPER_H
#define GBOYC_MAPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum {
	MBC_NONE,
	MBC_1,
	MBC_2,
	MBC_3,
	MBC_5,
	MBC_HUC1,
	MBC_HUC3,
	MBC_MMM01,
} MbcType;

typedef struct {
	uint8_t regs[5];
	uint8_t latched[5];
	uint8_t latch;
	time_t last;
} Rtc;

typedef struct {
	bool mapped;
	bool multiplex;
	bool mode_lock;
	uint8_t rom_low;
	uint8_t rom_mid;
	uint8_t rom_high;
	uint8_t rom_mask;
	uint8_t ram_low;
	uint8_t ram_high;
	uint8_t ram_mask;
} Mmm01;

typedef struct {
	MbcType type;
	bool multicart;
	bool has_timer;
	bool rumble;
	bool ram_enabled;
	bool mode;
	uint8_t io_mode;
	uint8_t bank1;
	uint8_t bank2;
	unsigned rom_bank;
	unsigned ram_bank;
	size_t rom_size;
	Rtc rtc;
	Mmm01 mmm;
} Mapper;

void mapper_reset(Mapper *m);
size_t mapper_rom_offset(const Mapper *m, uint16_t addr);
void mapper_write_rom(Mapper *m, uint16_t addr, uint8_t value);
uint8_t mapper_read_ram(const Mapper *m, const uint8_t *ram, size_t ram_size, uint16_t addr);
bool mapper_write_ram(Mapper *m, uint8_t *ram, size_t ram_size, uint16_t addr, uint8_t value);

void rtc_update(Rtc *r);

#endif

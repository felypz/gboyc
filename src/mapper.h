#ifndef GBOYC_MAPPER_H
#define GBOYC_MAPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MAPPER_ROM_BANK_SIZE   0x4000u
#define MAPPER_MMM01_MENU_SIZE 0x8000u /* MMM01 boots from the last 32 KiB of the file */
#define MAPPER_MBC2_RAM_SIZE   512u	   /* built-in 512 x 4-bit RAM */

typedef enum {
	MBC_NONE,
	MBC_1,
	MBC_2,
	MBC_3,
	MBC_5,
	MBC_HUC1,
	MBC_HUC3,
	MBC_MMM01,
	MBC_COUNT,
} MbcType;

#define RTC_REG_COUNT 5 /* seconds, minutes, hours, day low, day high/flags */
/* .sav layout after the RAM: regs[5] + 64-bit little-endian "last" timestamp. */
#define RTC_SAVE_SIZE (RTC_REG_COUNT + 8)

typedef struct {
	uint8_t regs[RTC_REG_COUNT];
	uint8_t latched[RTC_REG_COUNT];
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
	uint8_t huc_mode;		/* HuC1/HuC3 mode register */
	uint8_t mbc1_bank_low;	/* MBC1 5-bit ROM bank register */
	uint8_t mbc1_bank_high; /* MBC1 2-bit upper bank register */
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
/* .sav encoding of the live registers and timestamp, RTC_SAVE_SIZE bytes. */
void rtc_serialize(const Rtc *r, uint8_t *out);
void rtc_deserialize(Rtc *r, const uint8_t *in);

#endif

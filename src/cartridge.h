#ifndef GBOYC_CARTRIDGE_H
#define GBOYC_CARTRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Cartridge Cartridge;

/* persist == false never reads or writes the .sav file (the second console of a linked pair). */
Cartridge *cartridge_load(const char *path, bool persist);
void cartridge_free(Cartridge *c);
void cartridge_reset(Cartridge *c);
/* Writes battery RAM / RTC to the .sav file next to the ROM, if it changed. */
bool cartridge_flush_battery(Cartridge *c);
/* Drops unsaved battery changes so they won't be flushed (used after an emulator fault). */
void cartridge_discard_battery_changes(Cartridge *c);

const char *cartridge_title(const Cartridge *c);

uint8_t cartridge_read_rom(const Cartridge *c, uint16_t addr);
void cartridge_write_rom(Cartridge *c, uint16_t addr, uint8_t value);
unsigned cartridge_rom_bank(const Cartridge *c);
uint8_t cartridge_read_ram(const Cartridge *c, uint16_t addr);
void cartridge_write_ram(Cartridge *c, uint16_t addr, uint8_t value);

/* Savestate support. */
uint32_t cartridge_id(const Cartridge *c);
size_t cartridge_state_size(const Cartridge *c);
void cartridge_state_write(const Cartridge *c, uint8_t *out);
bool cartridge_state_check(const Cartridge *c, const uint8_t *in, size_t size);
/* Restores state exactly as it was (stepper rollback): battery dirty flag untouched. */
bool cartridge_state_restore(Cartridge *c, const uint8_t *in, size_t size);
/* Restores a user-loaded state: validates, then marks battery RAM dirty so it gets flushed. */
bool cartridge_state_load(Cartridge *c, const uint8_t *in, size_t size);

typedef struct {
	char title[17];
	char mapper[40];
	unsigned rom_kib;
	unsigned ram_bytes;
	bool battery;
	bool cgb;				  /* header says the game has Game Boy Color features */
	bool header_ok;			  /* header checksum (0x14D) matches */
	uint8_t version;		  /* mask ROM version number */
	uint16_t global_checksum; /* as stored in the header; the hardware never checks it */
} CartridgeInfo;

void cartridge_info(const Cartridge *c, CartridgeInfo *out);
void cartridge_log_info(const Cartridge *c);

#endif

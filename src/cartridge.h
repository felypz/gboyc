#ifndef GBOYC_CARTRIDGE_H
#define GBOYC_CARTRIDGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Cartridge Cartridge;

Cartridge *cartridge_load(const char *path);
void cartridge_free(Cartridge *c);
bool cartridge_save(Cartridge *c);

const char *cartridge_title(const Cartridge *c);

uint8_t cartridge_read_rom(const Cartridge *c, uint16_t addr);
void cartridge_write_rom(Cartridge *c, uint16_t addr, uint8_t value);
uint8_t cartridge_read_ram(const Cartridge *c, uint16_t addr);
void cartridge_write_ram(Cartridge *c, uint16_t addr, uint8_t value);

#endif

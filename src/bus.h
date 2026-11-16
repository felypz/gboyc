#ifndef GBOYC_BUS_H
#define GBOYC_BUS_H

#include <stdint.h>

#include "cartridge.h"

void bus_init(Cartridge *cart);
uint8_t bus_read(uint16_t addr);
void bus_write(uint16_t addr, uint8_t value);

#endif

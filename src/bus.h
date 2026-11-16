#ifndef GBOYC_BUS_H
#define GBOYC_BUS_H

#include <stdint.h>

#include "gb.h"

uint8_t bus_read(const Gb *gb, uint16_t addr);
void bus_write(Gb *gb, uint16_t addr, uint8_t value);

#endif

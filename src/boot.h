#ifndef GBOYC_BOOT_H
#define GBOYC_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_ROM_SIZE 256

bool boot_load(uint8_t rom[BOOT_ROM_SIZE]);

#endif

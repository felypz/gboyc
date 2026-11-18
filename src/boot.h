#ifndef GBOYC_BOOT_H
#define GBOYC_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_ROM_SIZE 256
#define BOOT_DIR	  "config/boot"

typedef enum {
	BOOT_OK,
	BOOT_ERR_OPEN,
	BOOT_ERR_SIZE,
	BOOT_ERR_INVALID,
	BOOT_ERR_WRITE,
} BootResult;

bool boot_load(const char *name, uint8_t rom[BOOT_ROM_SIZE]);
bool boot_check(const char *name);
BootResult boot_import(const char *src_path, char *name, size_t size);
const char *boot_result_text(BootResult result);

#endif

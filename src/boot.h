#ifndef GBOYC_BOOT_H
#define GBOYC_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_ROM_SIZE 256

typedef enum {
	BOOT_OK,
	BOOT_ERR_OPEN,
	BOOT_ERR_SIZE,
	BOOT_ERR_INVALID,
	BOOT_ERR_WRITE,
} BootResult;

/* Boot ROMs installed in the data dir's boot/ folder, addressed by file name. */
bool boot_load_installed(const char *name, uint8_t rom[BOOT_ROM_SIZE]);
bool boot_is_installed(const char *name);
/* Validates src_path and copies it into boot/; the stored name is written to name. */
BootResult boot_import(const char *src_path, char *name, size_t size);
/* Loads and validates an arbitrary file path (used by --boot-rom). */
BootResult boot_load_file(const char *path, uint8_t rom[BOOT_ROM_SIZE]);
const char *boot_result_text(BootResult result);

#endif

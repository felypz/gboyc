#ifndef GBOYC_LICENSE_H
#define GBOYC_LICENSE_H

#include <stdbool.h>
#include <stddef.h>

/* The LICENSE file, compiled into the binary (see the Makefile). */
extern const unsigned char LICENSE_TEXT[];
extern const unsigned long LICENSE_SIZE;

/*
 * Makes sure "<data>/LICENSE.txt" holds the license and returns its path, so the desktop's
 * default text editor can open it. Returns false if the file cannot be written.
 */
bool license_file(char *out, size_t size);

#endif

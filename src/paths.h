#ifndef GBOYC_PATHS_H
#define GBOYC_PATHS_H

#include <stdbool.h>
#include <stddef.h>

#define PATHS_MAX 4096

typedef enum {
	PATHS_CONFIG,
	PATHS_DATA,
} PathsRoot;

bool paths_file(PathsRoot root, char *out, size_t size, const char *sub, const char *name);
bool paths_dir(PathsRoot root, char *out, size_t size);
bool paths_make(PathsRoot root, const char *sub);

#endif

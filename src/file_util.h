#ifndef GBOYC_FILE_UTIL_H
#define GBOYC_FILE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "paths.h"

/*
 * Atomic file replacement: data goes to "<path>.tmp" and is renamed over
 * <path> on commit, so a crash never leaves a half-written file behind.
 */
typedef struct {
	FILE *file;
	char path[PATHS_MAX];
	char tmp[PATHS_MAX + 8];
} AtomicFile;

/* Opens "<path>.tmp" for writing ("w" or "wb"). Returns false on failure. */
bool atomic_file_open(AtomicFile *af, const char *path, const char *mode);
/* Closes and renames over the target. On any failure the temp file is removed. */
bool atomic_file_commit(AtomicFile *af);
/* Closes and removes the temp file without touching the target. */
void atomic_file_abort(AtomicFile *af);

bool file_write_atomic(const char *path, const void *data, size_t size);

#endif

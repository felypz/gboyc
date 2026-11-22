#include "file_util.h"

#include <stdio.h>

bool atomic_file_open(AtomicFile *af, const char *path, const char *mode) {
	int n = snprintf(af->path, sizeof af->path, "%s", path);

	if (n < 0 || (size_t)n >= sizeof af->path) {
		af->file = NULL;
		return false;
	}
	snprintf(af->tmp, sizeof af->tmp, "%s.tmp", af->path);
	af->file = fopen(af->tmp, mode);
	return af->file != NULL;
}

bool atomic_file_commit(AtomicFile *af) {
	if (!af->file) {
		return false;
	}

	bool ok = fclose(af->file) == 0;

	af->file = NULL;
	ok = ok && rename(af->tmp, af->path) == 0;
	if (!ok) {
		remove(af->tmp);
	}
	return ok;
}

void atomic_file_abort(AtomicFile *af) {
	if (af->file) {
		fclose(af->file);
		af->file = NULL;
	}
	remove(af->tmp);
}

bool file_write_atomic(const char *path, const void *data, size_t size) {
	AtomicFile af;

	if (!atomic_file_open(&af, path, "wb")) {
		return false;
	}
	if (size > 0 && fwrite(data, 1, size, af.file) != size) {
		atomic_file_abort(&af);
		return false;
	}
	return atomic_file_commit(&af);
}

#include "license.h"

#include <stdio.h>

#include "paths.h"

#define LICENSE_NAME "LICENSE.txt"

bool license_file(char *out, size_t size) {
	FILE *f;

	if (!paths_make(PATHS_DATA, NULL) || !paths_file(PATHS_DATA, out, size, NULL, LICENSE_NAME)) {
		return false;
	}
	if (!(f = fopen(out, "wb"))) {
		return false;
	}
	bool ok = fwrite(LICENSE_TEXT, 1, LICENSE_SIZE, f) == LICENSE_SIZE;

	return fclose(f) == 0 && ok;
}

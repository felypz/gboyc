#include "paths.h"

#include <stdio.h>
#include <stdlib.h>

#include "platform.h"

#define APP_DIR		 "gboyc"
#define FALLBACK_DIR "config" /* relative to the working dir, used when HOME is unknown */

/* Resolves (and caches) the XDG base directory for a root. */
static const char *base(PathsRoot root) {
	static char dirs[2][PATHS_MAX];
	static bool ready[2];
	char *path = dirs[root];

	if (ready[root]) {
		return path;
	}
	ready[root] = true;

	const char *xdg = getenv(root == PATHS_CONFIG ? "XDG_CONFIG_HOME" : "XDG_DATA_HOME");
	const char *home = getenv("HOME");
	const char *rel = root == PATHS_CONFIG ? ".config" : ".local/share";
	int n;

	if (xdg && xdg[0] == '/') {
		n = snprintf(path, PATHS_MAX, "%s/%s", xdg, APP_DIR);
	} else if (home && home[0]) {
		n = snprintf(path, PATHS_MAX, "%s/%s/%s", home, rel, APP_DIR);
	} else {
		n = snprintf(path, PATHS_MAX, "%s", FALLBACK_DIR);
	}
	if (n < 0 || n >= PATHS_MAX) {
		snprintf(path, PATHS_MAX, "%s", FALLBACK_DIR);
	}
	return path;
}

/* Builds "<base>[/sub][/name]"; sub and name may be NULL or empty. */
static bool join(char *out, size_t size, PathsRoot root, const char *sub, const char *name) {
	const char *parts[] = {base(root), sub, name};
	size_t len = 0;

	if (size == 0) {
		return false;
	}
	out[0] = '\0';

	for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) {
		if (!parts[i] || !parts[i][0]) {
			continue;
		}

		int n = snprintf(out + len, size - len, "%s%s", i > 0 ? "/" : "", parts[i]);

		if (n < 0 || (size_t)n >= size - len) {
			return false;
		}
		len += (size_t)n;
	}
	return true;
}

bool paths_file(PathsRoot root, char *out, size_t size, const char *sub, const char *name) {
	return join(out, size, root, sub, name);
}

bool paths_dir(PathsRoot root, char *out, size_t size) {
	return join(out, size, root, NULL, NULL);
}

bool paths_make(PathsRoot root, const char *sub) {
	char dir[PATHS_MAX];

	return join(dir, sizeof dir, root, sub, NULL) && platform_make_dir(dir);
}

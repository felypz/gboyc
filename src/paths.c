#include "paths.h"

#include <stdio.h>
#include <stdlib.h>

#include "platform.h"

#define APP_DIR		 "gboyc"
#define FALLBACK_DIR "config"

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

bool paths_file(PathsRoot root, char *out, size_t size, const char *sub, const char *name) {
	int n;

	if (sub && sub[0]) {
		n = snprintf(out, size, "%s/%s/%s", base(root), sub, name);
	} else {
		n = snprintf(out, size, "%s/%s", base(root), name);
	}
	return n >= 0 && (size_t)n < size;
}

bool paths_make(PathsRoot root, const char *sub) {
	char dir[PATHS_MAX];
	int n;

	if (sub && sub[0]) {
		n = snprintf(dir, sizeof dir, "%s/%s", base(root), sub);
	} else {
		n = snprintf(dir, sizeof dir, "%s", base(root));
	}
	return n >= 0 && (size_t)n < sizeof dir && platform_make_dir(dir);
}

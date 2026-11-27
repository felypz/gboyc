#include "recent.h"

#include <stdio.h>
#include <string.h>

#include "file_util.h"
#include "paths.h"

#define RECENT_FILE "recent.txt"

static char list[RECENT_MAX][PATHS_MAX];
static int count;

static bool file_path(char *out, size_t size) {
	return paths_file(PATHS_DATA, out, size, NULL, RECENT_FILE);
}

static void save(void) {
	char path[PATHS_MAX];
	AtomicFile out;

	if (!paths_make(PATHS_DATA, NULL) || !file_path(path, sizeof path) ||
		!atomic_file_open(&out, path, "w")) {
		return;
	}
	for (int i = 0; i < count; i++) {
		fprintf(out.file, "%s\n", list[i]);
	}
	atomic_file_commit(&out);
}

void recent_load(void) {
	char path[PATHS_MAX];
	FILE *f;

	count = 0;
	if (!file_path(path, sizeof path) || !(f = fopen(path, "r"))) {
		return;
	}
	while (count < RECENT_MAX && fgets(list[count], sizeof list[count], f)) {
		list[count][strcspn(list[count], "\r\n")] = '\0';
		if (list[count][0]) {
			count++;
		}
	}
	fclose(f);
}

static void drop(int index) {
	for (int i = index; i + 1 < count; i++) {
		memcpy(list[i], list[i + 1], sizeof list[i]);
	}
	count--;
}

void recent_remove(const char *path) {
	for (int i = 0; i < count; i++) {
		if (strcmp(list[i], path) == 0) {
			drop(i);
			save();
			return;
		}
	}
}

void recent_add(const char *path) {
	char copy[PATHS_MAX];

	if (strlen(path) >= sizeof copy) {
		return;
	}
	snprintf(copy, sizeof copy, "%s", path); /* path may point into the list */
	for (int i = 0; i < count; i++) {
		if (strcmp(list[i], copy) == 0) {
			drop(i);
			break;
		}
	}
	if (count == RECENT_MAX) {
		count--;
	}
	for (int i = count; i > 0; i--) {
		memcpy(list[i], list[i - 1], sizeof list[i]);
	}
	snprintf(list[0], sizeof list[0], "%s", copy);
	count++;
	save();
}

int recent_count(void) {
	return count;
}

const char *recent_get(int i) {
	return i >= 0 && i < count ? list[i] : NULL;
}

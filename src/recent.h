#ifndef GBOYC_RECENT_H
#define GBOYC_RECENT_H

#include <stdbool.h>

#define RECENT_MAX 5

/* The last ROMs that were opened, newest first. Kept in "<data>/recent.txt". */
void recent_load(void);
/* Moves (or inserts) path to the front and saves the list. */
void recent_add(const char *path);
void recent_remove(const char *path);
int recent_count(void);
/* Full path of entry i (0 = newest), or NULL. */
const char *recent_get(int i);

#endif

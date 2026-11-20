#ifndef GBOYC_GUARD_H
#define GBOYC_GUARD_H

#include <stdbool.h>

typedef void (*GuardFn)(void *arg);

void guard_install(void);
bool guard_run(GuardFn fn, void *arg);
int guard_signal(void);
void *guard_address(void);
const char *guard_signal_name(int sig);

#endif

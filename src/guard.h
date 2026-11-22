#ifndef GBOYC_GUARD_H
#define GBOYC_GUARD_H

#include <stdbool.h>

typedef void (*GuardFn)(void *arg);

void guard_install(void);
/* Runs fn; returns false if it was aborted by a fatal signal (SIGSEGV, SIGFPE, ...). */
bool guard_run(GuardFn fn, void *arg);
int guard_caught_signal(void);
void *guard_fault_address(void);
const char *guard_signal_name(int sig);

#endif

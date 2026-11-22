#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "guard.h"

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define GUARD_BACKTRACE 1
#endif
#endif

#define ALT_STACK_SIZE	(1 << 16)
#define BACKTRACE_DEPTH 32

static const int GUARDED_SIGNALS[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

static sigjmp_buf jump_env;
static volatile sig_atomic_t armed;
static volatile sig_atomic_t caught_signal;
static void *volatile caught_address;

const char *guard_signal_name(int sig) {
	switch (sig) {
	case SIGSEGV:
		return "SIGSEGV";
	case SIGBUS:
		return "SIGBUS";
	case SIGFPE:
		return "SIGFPE";
	case SIGILL:
		return "SIGILL";
	case SIGABRT:
		return "SIGABRT";
	default:
		return "signal";
	}
}

/* Raw write(2): stdio is not safe to use from a signal handler. */
static void write_stderr(const char *text) {
	ssize_t written = write(STDERR_FILENO, text, strlen(text));

	(void)written;
}

/*
 * A signal outside guard_run(): print what we can and restore the default
 * action, so returning from the handler re-raises the fault and the process
 * dies normally. backtrace_symbols_fd() is not formally async-signal-safe,
 * but the process is going down anyway and guard_install() pre-loads the
 * unwinder so it does not allocate here.
 */
static void report_fatal_signal(int sig) {
	write_stderr("\ngboyc: fatal error: ");
	write_stderr(guard_signal_name(sig));
	write_stderr("\n");
#ifdef GUARD_BACKTRACE
	void *frames[BACKTRACE_DEPTH];
	int count = backtrace(frames, BACKTRACE_DEPTH);

	backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
	signal(sig, SIG_DFL);
}

static void on_signal(int sig, siginfo_t *info, void *context) {
	(void)context;

	if (armed) {
		armed = 0;
		caught_signal = sig;
		caught_address = info ? info->si_addr : NULL;
		siglongjmp(jump_env, 1);
	}
	report_fatal_signal(sig);
}

void guard_install(void) {
	static char alt_stack[ALT_STACK_SIZE];
	stack_t stack = {.ss_sp = alt_stack, .ss_size = sizeof alt_stack};
	struct sigaction action;

#ifdef GUARD_BACKTRACE
	void *warmup[1];

	backtrace(warmup, 1);
#endif
	sigaltstack(&stack, NULL);

	memset(&action, 0, sizeof action);
	action.sa_sigaction = on_signal;
	action.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&action.sa_mask);

	for (size_t i = 0; i < sizeof GUARDED_SIGNALS / sizeof *GUARDED_SIGNALS; i++) {
		sigaction(GUARDED_SIGNALS[i], &action, NULL);
	}
}

bool guard_run(GuardFn fn, void *arg) {
	if (sigsetjmp(jump_env, 1) != 0) {
		return false;
	}

	armed = 1;
	fn(arg);
	armed = 0;
	return true;
}

int guard_caught_signal(void) {
	return (int)caught_signal;
}

void *guard_fault_address(void) {
	return caught_address;
}

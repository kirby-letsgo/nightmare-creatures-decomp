#include "port/runtime.h"

#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <unistd.h>
#define NC_HAVE_BACKTRACE 1
#endif

/* Everything logged also goes to nightmare.log (current directory), so crashes can be reported
 * without a terminal. */
#define LOG_PATH "nightmare.log"

static FILE *log_file;

static void log_line(const char *prefix, const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    fputs(prefix, stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    if (log_file != NULL) {
        fputs(prefix, log_file);
        vfprintf(log_file, fmt, copy);
        fputc('\n', log_file);
        fflush(log_file);
    }
    va_end(copy);
}

void nc_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_line("", fmt, ap);
    va_end(ap);
}

/* Writes the native backtrace; recompiled functions are named <module>_<address>, so this is
 * also the guest call stack. */
static void write_backtrace(int fd) {
#ifdef NC_HAVE_BACKTRACE
    void *addrs[64];
    int n = backtrace(addrs, 64);
    backtrace_symbols_fd(addrs, n, fd);
#else
    (void)fd;
#endif
}

static void dump_crash(void) {
    const char *header = "--- backtrace (guest functions are <module>_<address>) ---\n";
    if (log_file != NULL) {
        fputs(header, log_file);
        fflush(log_file);
#ifdef NC_HAVE_BACKTRACE
        write_backtrace(fileno(log_file));
#endif
    }
    fputs(header, stderr);
    write_backtrace(2);
}

_Noreturn void nc_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_line("FATAL: ", fmt, ap);
    va_end(ap);
    dump_crash();
    abort();
}

static void on_signal(int sig) {
    char msg[64];
    snprintf(msg, sizeof msg, "FATAL: signal %d\n", sig);
    if (log_file != NULL) {
        fputs(msg, log_file);
    }
    fputs(msg, stderr);
    dump_crash();
    signal(sig, SIG_DFL);
    raise(sig);
}

void nc_log_init(void) {
    log_file = fopen(LOG_PATH, "w");
    signal(SIGSEGV, on_signal);
    signal(SIGILL, on_signal);
    signal(SIGFPE, on_signal);
#ifdef SIGBUS
    signal(SIGBUS, on_signal);
#endif
}

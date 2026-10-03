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

/* Everything logged also goes to nightmare.log (see nc_log_init), so crashes can be reported
 * without a terminal. */

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

void nc_log_init(const char *path) {
    log_file = fopen(path, "w");
    signal(SIGSEGV, on_signal);
    signal(SIGILL, on_signal);
    signal(SIGFPE, on_signal);
#ifdef SIGBUS
    signal(SIGBUS, on_signal);
#endif
}

#ifdef NC_WATCHPOINTS
u32 nc_watch_addr = 0xFFFFFFFFu;

__attribute__((constructor)) static void watch_init(void) {
    const char *env = getenv("NC_WATCH");
    if (env != NULL) {
        nc_watch_addr = (u32)strtoul(env, NULL, 16) & 0x1FFFFCu;
    }
}

/* NC_WATCH_SKIP=N ignores the first N hits, NC_WATCH_FROM=F ignores hits before VBlank F;
 * only stores that cover the exact NC_WATCH byte count; repeated call stacks are reported once. */
extern unsigned nc_frame_count;

void nc_watch_hit(u32 addr, u32 value, int size) {
    static long from = -1;
    static u32 exact;
    if (from < 0) {
        const char *env = getenv("NC_WATCH_FROM");
        from = env ? atol(env) : 0;
        exact = (u32)strtoul(getenv("NC_WATCH"), NULL, 16) & 0x1FFFFFu;
    }
    u32 lo = addr & 0x1FFFFFu;
    if (nc_frame_count < (unsigned)from || exact < lo || exact >= lo + (u32)size) {
        return;
    }
    static long skip = -1;
    static unsigned reported;
    static void *seen[64];
    static int nseen;
    if (skip < 0) {
        const char *env = getenv("NC_WATCH_SKIP");
        skip = env ? atol(env) : 0;
    }
    if (skip > 0) {
        skip--;
        return;
    }
#ifdef NC_HAVE_BACKTRACE
    void *addrs[12];
    int n = backtrace(addrs, 12);
    void *key = n > 2 ? addrs[2] : NULL;
    for (int i = 0; i < nseen; i++) {
        if (seen[i] == key) {
            return;
        }
    }
    if (nseen < 64) {
        seen[nseen++] = key;
    }
    if (reported++ >= 40) {
        return;
    }
    nc_log("watch: write%d 0x%08X = 0x%08X", size * 8, addr, value);
    char **names = backtrace_symbols(addrs, n);
    for (int i = 2; i < n && names != NULL; i++) {
        if (strstr(names[i], "_exe_") || strstr(names[i], "slus_")) {
            nc_log("    %s", names[i]);
        }
    }
    free(names);
#else
    (void)reported;
    nc_log("watch: write%d 0x%08X = 0x%08X", size * 8, addr, value);
#endif
}
#endif

#ifdef NC_WATCHPOINTS
/* NC_COVERAGE=<first>,<last>,<file>: counts guest function entries between two VBlank frame
 * numbers and writes "address count" lines to <file>. With NC_COVERAGE_CALLERS=<address>, calls
 * to that function are counted per return address instead. */
extern unsigned nc_frame_count;
#define COV_SLOTS 8192

static u32 cov_addr[COV_SLOTS];
static u32 cov_hits[COV_SLOTS];

void nc_fn_enter(u32 addr, u32 ra) {
    static int state = -1;
    static unsigned first, last;
    static char path[256];
    static u32 callers_of;
    if (state < 0) {
        const char *env = getenv("NC_COVERAGE");
        state = env != NULL && sscanf(env, "%u,%u,%255s", &first, &last, path) == 3 ? 1 : 0;
        const char *callers = getenv("NC_COVERAGE_CALLERS");
        callers_of = callers ? (u32)strtoul(callers, NULL, 16) : 0;
    }
    if (callers_of != 0) {
        if (addr != callers_of) {
            return;
        }
        addr = ra; /* count by call site */
    }
    if (state != 1 || nc_frame_count < first) {
        return;
    }
    if (nc_frame_count > last) {
        FILE *f = fopen(path, "w");
        for (int i = 0; f != NULL && i < COV_SLOTS; i++) {
            if (cov_hits[i] != 0) {
                fprintf(f, "%08X %u\n", cov_addr[i], cov_hits[i]);
            }
        }
        if (f != NULL) {
            fclose(f);
        }
        state = 2;
        return;
    }
    u32 h = (addr >> 2) * 2654435761u % COV_SLOTS;
    while (cov_hits[h] != 0 && cov_addr[h] != addr) {
        h = (h + 1) % COV_SLOTS;
    }
    cov_addr[h] = addr;
    cov_hits[h]++;
}
#endif

/* Shadow mode: checks handwritten routines against the original instructions during real runs.
 *
 * With NC_SHADOW=1, each call to a routine in the native registry (tools/gen_registry.py)
 * snapshots the whole machine, runs the handwritten routine, records the result, restores the
 * snapshot and interprets the original instructions with every nested call interpreted too. The
 * two results are compared and the interpreter's is kept, so a broken routine cannot derail the
 * run. Only the outermost call is shadowed; calls made during either run execute normally.
 *
 * The snapshot covers what the save-state serializers cover (CPU, RAM, scratchpad, cycle
 * counters, BIOS, interrupt controller, timers, DMA, GPU, SPU, CD-ROM, MDEC), plus the BIOS
 * interrupt context. Effects outside it cannot be undone, so a native run that reaches one (a
 * VBlank, which presents a frame and polls input; a hook; Exec; a memory card write) is abandoned
 * before it happens: the state is restored and the call runs normally, unchecked.
 *
 * NC_SHADOW_LIMIT=N   calls checked per routine (default 50)
 * NC_SHADOW_FATAL=1   exit at the first mismatch (otherwise exit with status 1 at the end)
 * NC_SHADOW_REPORT=f  append one line per registry routine to f at exit (tools/test_shadow.py) */
#include "port/shadow.h"

#include "port/runtime.h"
#include "port/savestate.h"

#include "port/interp.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nc_shadow_enabled = 1;
bool nc_shadow_guard;
bool nc_shadow_reference;

/* --- snapshots ---------------------------------------------------------------------------- */

static CPUState *regs; /* the caller's CPUState, when it is not nc_cpu */

static void regs_serialize(StateIO *io) {
    if (regs != NULL) {
        state_io(io, regs, sizeof *regs);
    }
}

typedef struct Segment {
    const char *name;
    void (*io)(StateIO *io);
} Segment;

enum { SEG_REGS, SEG_CPU, SEG_COUNT = 11 };
static const Segment segments[SEG_COUNT] = {
    {"registers", regs_serialize},    {"cpu", cpu_serialize},   {"bios", bios_shadow_serialize},
    {"dispatch", dispatch_serialize}, {"irq", irq_serialize},   {"timers", timers_serialize},
    {"dma", dma_serialize},           {"gpu", gpu_serialize},   {"spu", spu_serialize},
    {"cdrom", cdrom_serialize},       {"mdec", mdec_serialize},
};

typedef struct Snapshot {
    StateIO io;
    size_t off[SEG_COUNT + 1];
} Snapshot;

static void capture(Snapshot *s) {
    s->io.loading = false;
    s->io.len = s->io.pos = 0;
    s->io.error = false;
    for (int i = 0; i < SEG_COUNT; i++) {
        s->off[i] = s->io.len;
        segments[i].io(&s->io);
    }
    s->off[SEG_COUNT] = s->io.len;
    if (s->io.error) {
        NC_FATAL("shadow: out of memory for a snapshot");
    }
}

static size_t seg_len(const Snapshot *s, int i) {
    return s->off[i + 1] - s->off[i];
}

static const u8 *seg_data(const Snapshot *s, int i) {
    return s->io.buf + s->off[i];
}

static bool seg_equal(const Snapshot *a, const Snapshot *b, int i) {
    return seg_len(a, i) == seg_len(b, i) &&
           memcmp(seg_data(a, i), seg_data(b, i), seg_len(a, i)) == 0;
}

/* Puts back `from` wherever `now` (the current state) differs from it. Unchanged devices are not
 * reloaded, so a routine that only touches RAM does not flush the GPU's texture cache. */
static void restore(const Snapshot *from, const Snapshot *now) {
    for (int i = SEG_COUNT - 1; i >= 0; i--) {
        if (seg_equal(from, now, i)) {
            continue;
        }
        StateIO io = {.loading = true, .buf = (u8 *)seg_data(from, i), .len = seg_len(from, i)};
        segments[i].io(&io);
        if (io.error || io.pos != io.len) {
            NC_FATAL("shadow: cannot restore the %s state", segments[i].name);
        }
    }
}

/* --- differences -------------------------------------------------------------------------- */

static u32 word_at(const u8 *p, size_t len, size_t off) {
    u32 w = 0;
    memcpy(&w, p + off, off + 4 <= len ? 4 : len - off);
    return w;
}

static void register_name(size_t off, char *out, size_t cap) {
    if (off < offsetof(CPUState, hi)) {
        snprintf(out, cap, "r%zu", off / 4);
    } else if (off < offsetof(CPUState, lo)) {
        snprintf(out, cap, "hi");
    } else if (off < offsetof(CPUState, cop0)) {
        snprintf(out, cap, "lo");
    } else if (off < offsetof(CPUState, gte)) {
        snprintf(out, cap, "cop0 r%zu", (off - offsetof(CPUState, cop0)) / 4);
    } else if (off < offsetof(CPUState, gte.ctrl)) {
        snprintf(out, cap, "gte data r%zu", (off - offsetof(CPUState, gte.data)) / 4);
    } else if (off < offsetof(CPUState, gte.flag)) {
        snprintf(out, cap, "gte ctrl r%zu", (off - offsetof(CPUState, gte.ctrl)) / 4);
    } else if (off < offsetof(CPUState, budget)) {
        snprintf(out, cap, "gte flag");
    } else {
        snprintf(out, cap, "cycle budget");
    }
}

/* Names the word at `off` in segment `seg`. */
static void location_name(int seg, size_t off, char *out, size_t cap) {
    if (seg == SEG_REGS || (seg == SEG_CPU && off < sizeof(CPUState))) {
        register_name(off, out, cap);
        return;
    }
    if (seg == SEG_CPU) {
        size_t ram = off - sizeof(CPUState);
        if (ram < RAM_SIZE) {
            snprintf(out, cap, "RAM 0x%08zX", 0x80000000u + ram);
            return;
        }
        size_t scratch = ram - RAM_SIZE;
        if (scratch < SCRATCH_SIZE) {
            snprintf(out, cap, "scratchpad 0x%08zX", SCRATCH_BASE + scratch);
            return;
        }
        static const char *const counters[] = {"emulated cycles",    "emulated cycles (high)",
                                               "VBlank count",       "next VBlank",
                                               "next VBlank (high)", "poll budget start"};
        size_t k = (scratch - SCRATCH_SIZE) / 4;
        snprintf(out, cap, "%s", k < 6 ? counters[k] : "cpu counters");
        return;
    }
    snprintf(out, cap, "%s state +0x%zX", segments[seg].name, off);
}

/* Describes the first difference between two snapshots and counts the differing words. */
static bool first_difference(const Snapshot *a, const char *a_name, const Snapshot *b,
                             const char *b_name, char *out, size_t cap) {
    unsigned words = 0;
    bool found = false;
    for (int i = 0; i < SEG_COUNT; i++) {
        size_t len = seg_len(a, i);
        if (len != seg_len(b, i)) {
            if (!found) {
                snprintf(out, cap, "%s state size %zu vs %zu", segments[i].name, len,
                         seg_len(b, i));
                found = true;
            }
            words++;
            continue;
        }
        const u8 *pa = seg_data(a, i), *pb = seg_data(b, i);
        if (memcmp(pa, pb, len) == 0) {
            continue;
        }
        for (size_t off = 0; off < len; off += 4) {
            u32 wa = word_at(pa, len, off), wb = word_at(pb, len, off);
            if (wa == wb) {
                continue;
            }
            words++;
            if (!found) {
                char where[64];
                location_name(i, off, where, sizeof where);
                snprintf(out, cap, "%s: %s 0x%08X, %s 0x%08X", where, a_name, wa, b_name, wb);
                found = true;
            }
        }
    }
    if (found) {
        size_t n = strlen(out);
        snprintf(out + n, cap - n, " (%u differing word%s)", words, words == 1 ? "" : "s");
    }
    return found;
}

/* --- statistics --------------------------------------------------------------------------- */

typedef struct Stat {
    const char *name;
    const char *abandoned_why;
    unsigned calls, checked, declined, abandoned, mismatches;
} Stat;

#define LAUNCHER NC_MOD_COUNT
static const char *const module_names[NC_MOD_COUNT + 1] = {
    "psx_exe",     "psx2_exe",    "credits_exe", "stream1_exe",
    "stream2_exe", "stream3_exe", "slus_005_82",
};
static Stat *stats[NC_MOD_COUNT + 1];
static unsigned limit = 50, total_mismatches;
static bool fatal, summarized, any_calls;
static const char *report_path;

/* Log output is muted during native runs (they are repeated); shadow's own messages are not. */
#define SHADOW_LOG(...)                                                                            \
    do {                                                                                           \
        bool muted_ = nc_shadow_guard;                                                             \
        nc_shadow_guard = false;                                                                   \
        NC_LOG(__VA_ARGS__);                                                                       \
        nc_shadow_guard = muted_;                                                                  \
    } while (0)

static Stat *stat_for(u32 addr, int *mod_out) {
    addr = (addr & 0x1FFFFFFFu) | 0x80000000u;
    int mod = addr >= NC_LAUNCHER_LO && addr < NC_OVERLAY_LO ? LAUNCHER : (int)nc_active_module();
    if (mod < 0) {
        return NULL;
    }
    unsigned len;
    const NcFuncEntry *table = nc_dispatch_table(mod, &len);
    unsigned lo = 0, hi = len;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (table[mid].addr < addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == len || table[lo].addr != addr) {
        return NULL;
    }
    *mod_out = mod;
    return &stats[mod][lo];
}

/* --- nesting ------------------------------------------------------------------------------ */

/* A call made during a native run is shadowed too (from the state that run reached), so routines
 * only ever called from other handwritten routines are still checked. Reference runs interpret
 * everything, so they contain no shadowed calls. */
enum { MAX_DEPTH = 8 };

typedef struct Level {
    Snapshot before, native_after, interp_after;
    jmp_buf abandon, rfe;
    CPUState *regs;
    Stat *stat;
    int mod;
    u32 addr, return_sp;
    bool in_reference;
} Level;

static Level levels[MAX_DEPTH];
static int depth;                  /* shadowed calls in progress */
static int native_runs[MAX_DEPTH]; /* levels whose native run is in progress, innermost last */
static int native_count;
static const char *abandon_why;

static void set_guard(void) {
    nc_shadow_guard = native_count > 0;
}

static void snap(const Level *l, Snapshot *s) {
    regs = l->regs;
    capture(s);
}

static void unsnap(const Level *l, const Snapshot *from, const Snapshot *now) {
    regs = l->regs;
    restore(from, now);
}

/* --- summary ------------------------------------------------------------------------------ */

/* Logs a mismatch and appends it to the report. */
static void report_mismatch(const Level *l, const char *what) {
    l->stat->mismatches++;
    total_mismatches++;
    char line[512];
    snprintf(line, sizeof line, "%s %08X %s, call %u at VBlank %u: %s", module_names[l->mod],
             l->addr, l->stat->name, l->stat->calls, nc_frame_count, what);
    SHADOW_LOG("shadow: MISMATCH %s", line);
    FILE *report = report_path ? fopen(report_path, "a") : NULL;
    if (report) {
        fprintf(report, "mismatch %s\n", line);
        fclose(report);
    }
}

static void summary(void) {
    if (summarized || !nc_shadow_enabled) {
        return;
    }
    summarized = true;
    nc_shadow_guard = false;
    for (int i = 0; i < depth; i++) {
        const Level *l = &levels[i];
        if (l->in_reference) {
            char what[128];
            snprintf(what, sizeof what,
                     "the original had not returned (to $sp 0x%08X) when the process exited",
                     l->return_sp);
            report_mismatch(l, what);
        }
    }
    FILE *report = report_path ? fopen(report_path, "a") : NULL;
    unsigned routines = 0, reached = 0, checked_routines = 0, checked = 0, declined = 0;
    for (int m = 0; m <= NC_MOD_COUNT; m++) {
        unsigned len, mod_reached = 0, mod_checked = 0;
        const NcFuncEntry *table = nc_dispatch_table(m, &len);
        for (unsigned i = 0; i < len; i++) {
            const Stat *s = &stats[m][i];
            mod_reached += s->calls != 0;
            mod_checked += s->checked != 0;
            checked += s->checked;
            declined += s->declined;
            if (report) {
                fprintf(report, "routine %s %08X %u %u %u %u %u %s %s\n", module_names[m],
                        table[i].addr, s->calls, s->checked, s->declined, s->abandoned,
                        s->mismatches, s->abandoned_why ? s->abandoned_why : "-",
                        s->name ? s->name : "-");
            }
        }
        routines += len;
        reached += mod_reached;
        checked_routines += mod_checked;
        if (len) {
            NC_LOG("shadow: %-11s %3u routines, %3u reached, %3u checked", module_names[m], len,
                   mod_reached, mod_checked);
        }
    }
    for (int m = 0; m <= NC_MOD_COUNT; m++) {
        unsigned len;
        const NcFuncEntry *table = nc_dispatch_table(m, &len);
        for (unsigned i = 0; i < len; i++) {
            const Stat *s = &stats[m][i];
            if (s->calls && !s->checked) {
                NC_LOG("shadow: never checked: %s %08X %s (%u calls, abandoned at %s)",
                       module_names[m], table[i].addr, s->name, s->calls,
                       s->abandoned_why ? s->abandoned_why : "the nesting limit");
            }
        }
        char line[1024];
        size_t n = 0;
        for (unsigned i = 0; i < len; i++) {
            if (!stats[m][i].calls && n + 10 < sizeof line) {
                n += (size_t)snprintf(line + n, sizeof line - n, " %08X", table[i].addr);
            }
        }
        if (n) {
            NC_LOG("shadow: never reached in %s:%s", module_names[m], line);
        }
    }
    if (!any_calls) {
        NC_LOG("shadow: no routine went through the native registry (needs make build-native)");
    }
    NC_LOG("shadow: %u of %u routines reached, %u checked; %u calls checked (%u declined); "
           "%u mismatches",
           reached, routines, checked_routines, checked, declined, total_mismatches);
    if (report) {
        fclose(report);
    }
    if (total_mismatches) {
        fflush(NULL);
        _Exit(EXIT_FAILURE);
    }
}

void nc_shadow_init(void) {
    static bool done;
    if (done) {
        return;
    }
    done = true;
    const char *on = getenv("NC_SHADOW");
    nc_shadow_enabled = on != NULL && on[0] != '\0' && strcmp(on, "0") != 0;
    if (!nc_shadow_enabled) {
        return;
    }
    const char *n = getenv("NC_SHADOW_LIMIT");
    if (n != NULL) {
        limit = (unsigned)strtoul(n, NULL, 10);
    }
    const char *f = getenv("NC_SHADOW_FATAL");
    fatal = f != NULL && strcmp(f, "0") != 0;
    report_path = getenv("NC_SHADOW_REPORT");
    for (int m = 0; m <= NC_MOD_COUNT; m++) {
        unsigned len;
        nc_dispatch_table(m, &len);
        stats[m] = calloc(len ? len : 1, sizeof(Stat));
    }
    atexit(summary);
    NC_LOG("shadow: checking native routines against the interpreter (%u calls each)", limit);
}

/* --- shadowed calls ----------------------------------------------------------------------- */

enum Outcome { RETURNED, DECLINED, LEFT_INTERRUPT, ABANDONED };
static const char *const outcome_names[] = {"returned", "declined", "left the interrupt",
                                            "abandoned"};

/* A host effect during a native run: abandon the innermost one. Levels inside it (and their
 * reference runs) are dropped with it. */
_Noreturn void nc_shadow_abort(const char *why) {
    abandon_why = why;
    longjmp(levels[native_runs[native_count - 1]].abandon, 1);
}

static enum Outcome run_native(int index, CPUState *c, bool (*native)(CPUState *c), bool in_irq) {
    Level *l = &levels[index];
    if (setjmp(l->abandon) != 0) {
        for (int i = index; i < depth; i++) {
            levels[i].in_reference = false;
        }
        depth = index + 1;
        while (native_count > 0 && native_runs[native_count - 1] >= index) {
            native_count--;
        }
        nc_shadow_reference = false;
        set_guard();
        return ABANDONED;
    }
    if (in_irq) {
        if (setjmp(l->rfe) != 0) {
            native_count--;
            set_guard();
            return LEFT_INTERRUPT;
        }
    }
    native_runs[native_count++] = index;
    set_guard();
    bool accepted = native(c);
    native_count--;
    set_guard();
    return accepted ? RETURNED : DECLINED;
}

/* A routine can return on another stack (the launcher's stack switch): the original then
 * returns once it reaches $ra with the stack pointer the native routine returned with. */
static enum Outcome run_reference(int index, CPUState *c, bool in_irq) {
    Level *l = &levels[index];
    if (in_irq) {
        if (setjmp(l->rfe) != 0) {
            nc_shadow_reference = false;
            l->in_reference = false;
            return LEFT_INTERRUPT;
        }
    }
    l->in_reference = true;
    nc_shadow_reference = true;
    if (l->return_sp == c->r[29]) {
        interp_call(c, l->addr);
    } else {
        interp_continue_call(c, l->addr, c->r[31], l->return_sp);
    }
    nc_shadow_reference = false;
    l->in_reference = false;
    return RETURNED;
}

static void mismatch(const Level *l, const char *what) {
    report_mismatch(l, what);
    if (fatal) {
        exit(EXIT_FAILURE);
    }
}

bool nc_shadow_call(CPUState *c, u32 addr, const char *name, bool (*native)(CPUState *c)) {
    nc_shadow_init();
    if (!nc_shadow_enabled) {
        return false;
    }
    int mod;
    Stat *s = stat_for(addr, &mod);
    if (s == NULL) {
        return false;
    }
    s->name = name;
    s->calls++;
    any_calls = true;
    if (nc_shadow_reference || depth == MAX_DEPTH || s->checked >= limit || s->abandoned >= limit) {
        return false;
    }
    int index = depth++;
    Level *l = &levels[index];
    l->regs = c == &nc_cpu ? NULL : c;
    l->stat = s;
    l->mod = mod;
    l->addr = addr;
    bool in_irq = bios_in_exception();
    jmp_buf *outer_return = bios_set_exception_return(&l->rfe);

    snap(l, &l->before);
    enum Outcome native_outcome = run_native(index, c, native, in_irq);
    l->return_sp = c->r[29];
    snap(l, &l->native_after);
    unsnap(l, &l->before, &l->native_after);
    if (native_outcome == ABANDONED) {
        s->abandoned++;
        s->abandoned_why = abandon_why;
        bios_set_exception_return(outer_return);
        depth--;
        return false; /* run it for real */
    }
    char what[256];
    s->checked++;
    if (native_outcome == DECLINED) {
        /* Declining must leave no trace. There is nothing to compare the original with, so it
         * runs as it normally would (it can span frames, and nested calls are shadowed). */
        s->declined++;
        bios_set_exception_return(outer_return);
        depth--;
        if (first_difference(&l->native_after, "after", &l->before, "before", what, sizeof what)) {
            char declined[300];
            snprintf(declined, sizeof declined, "declined but changed the state: %s", what);
            mismatch(l, declined);
        }
        interp_call(c, addr);
        return true;
    }

    bios_set_exception_return(&l->rfe);
    enum Outcome interp_outcome = run_reference(index, c, in_irq);
    snap(l, &l->interp_after);
    if (native_outcome != interp_outcome) {
        snprintf(what, sizeof what, "native %s, interpreter %s", outcome_names[native_outcome],
                 outcome_names[interp_outcome]);
        mismatch(l, what);
    } else if (first_difference(&l->native_after, "native", &l->interp_after, "interpreter", what,
                                sizeof what)) {
        mismatch(l, what);
    }
    bios_set_exception_return(outer_return);
    depth--;
    if (interp_outcome == LEFT_INTERRUPT) {
        longjmp(*outer_return, 1); /* ReturnFromException, as the original did */
    }
    return true;
}

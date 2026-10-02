/* Interface between recompiled MIPS code (gen/) and the native runtime (src/port/). */
#ifndef NC_PORT_RECOMP_H
#define NC_PORT_RECOMP_H

#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint64_t u64;

#define RAM_SIZE 0x200000u
#define SCRATCH_BASE 0x1F800000u
#define SCRATCH_SIZE 0x400u

typedef struct GTEState {
    u32 data[32];
    u32 ctrl[32];
    u32 flag;
} GTEState;

typedef struct CPUState {
    u32 r[32];
    u32 hi, lo;
    u32 cop0[32];
    GTEState gte;
    s32 budget; /* cycles left before the next scheduled event must be serviced */
} CPUState;

typedef void (*NcFunc)(CPUState *c);

typedef struct NcFuncEntry {
    u32 addr;
    NcFunc fn;
} NcFuncEntry;

extern u8 nc_ram[RAM_SIZE];
extern u8 nc_scratch[SCRATCH_SIZE];

#define R(n) (c->r[(n)])

/* --- memory ---------------------------------------------------------------------------- */

u32 io_read(u32 phys, int size);
void io_write(u32 phys, u32 value, int size);

static inline u32 nc_phys(u32 addr) {
    return addr & 0x1FFFFFFFu;
}

static inline u8 *nc_fast_ptr(u32 addr) {
    u32 p = nc_phys(addr);
    if (p < 0x00800000u) {
        return nc_ram + (p & (RAM_SIZE - 1));
    }
    if ((p & ~(SCRATCH_SIZE - 1)) == SCRATCH_BASE) {
        return nc_scratch + (p & (SCRATCH_SIZE - 1));
    }
    return NULL;
}

#define NC_DEFINE_LOAD(name, type, size)                                                           \
    static inline u32 name(u32 addr) {                                                             \
        u8 *p = nc_fast_ptr(addr);                                                                 \
        if (p != NULL) {                                                                           \
            type v;                                                                                \
            memcpy(&v, p, sizeof v);                                                               \
            return v;                                                                              \
        }                                                                                          \
        return (type)io_read(nc_phys(addr), size);                                                 \
    }
#define NC_DEFINE_STORE(name, type, size)                                                          \
    static inline void name(u32 addr, type value) {                                                \
        u8 *p = nc_fast_ptr(addr);                                                                 \
        if (p != NULL) {                                                                           \
            memcpy(p, &value, sizeof value);                                                       \
            return;                                                                                \
        }                                                                                          \
        io_write(nc_phys(addr), value, size);                                                      \
    }

NC_DEFINE_LOAD(MEM_R8, u8, 1)
NC_DEFINE_LOAD(MEM_R16, u16, 2)
NC_DEFINE_LOAD(MEM_R32, u32, 4)
NC_DEFINE_STORE(MEM_W8, u8, 1)
NC_DEFINE_STORE(MEM_W16, u16, 2)
NC_DEFINE_STORE(MEM_W32, u32, 4)

/* Unaligned load/store halves (lwl/lwr/swl/swr), little-endian semantics. */
static inline u32 nc_lwl(CPUState *c, u32 addr, u32 old) {
    (void)c;
    u32 w = MEM_R32(addr & ~3u);
    switch (addr & 3) {
    case 0:
        return (old & 0x00FFFFFFu) | (w << 24);
    case 1:
        return (old & 0x0000FFFFu) | (w << 16);
    case 2:
        return (old & 0x000000FFu) | (w << 8);
    default:
        return w;
    }
}
static inline u32 nc_lwr(CPUState *c, u32 addr, u32 old) {
    (void)c;
    u32 w = MEM_R32(addr & ~3u);
    switch (addr & 3) {
    case 0:
        return w;
    case 1:
        return (old & 0xFF000000u) | (w >> 8);
    case 2:
        return (old & 0xFFFF0000u) | (w >> 16);
    default:
        return (old & 0xFFFFFF00u) | (w >> 24);
    }
}
static inline void nc_swl(CPUState *c, u32 addr, u32 v) {
    (void)c;
    u32 a = addr & ~3u, w = MEM_R32(a);
    switch (addr & 3) {
    case 0:
        w = (w & 0xFFFFFF00u) | (v >> 24);
        break;
    case 1:
        w = (w & 0xFFFF0000u) | (v >> 16);
        break;
    case 2:
        w = (w & 0xFF000000u) | (v >> 8);
        break;
    default:
        w = v;
        break;
    }
    MEM_W32(a, w);
}
static inline void nc_swr(CPUState *c, u32 addr, u32 v) {
    (void)c;
    u32 a = addr & ~3u, w = MEM_R32(a);
    switch (addr & 3) {
    case 0:
        w = v;
        break;
    case 1:
        w = (w & 0x000000FFu) | (v << 8);
        break;
    case 2:
        w = (w & 0x0000FFFFu) | (v << 16);
        break;
    default:
        w = (w & 0x00FFFFFFu) | (v << 24);
        break;
    }
    MEM_W32(a, w);
}

/* --- arithmetic ------------------------------------------------------------------------- */

static inline void nc_mult(CPUState *c, u32 a, u32 b) {
    s64 r = (s64)(s32)a * (s64)(s32)b;
    c->lo = (u32)r;
    c->hi = (u32)((u64)r >> 32);
}
static inline void nc_multu(CPUState *c, u32 a, u32 b) {
    u64 r = (u64)a * (u64)b;
    c->lo = (u32)r;
    c->hi = (u32)(r >> 32);
}
static inline void nc_div(CPUState *c, u32 a, u32 b) {
    s32 n = (s32)a, d = (s32)b;
    if (d == 0) { /* R3000A results for division by zero */
        c->lo = n >= 0 ? 0xFFFFFFFFu : 1u;
        c->hi = a;
    } else if (a == 0x80000000u && d == -1) {
        c->lo = 0x80000000u;
        c->hi = 0;
    } else {
        c->lo = (u32)(n / d);
        c->hi = (u32)(n % d);
    }
}
static inline void nc_divu(CPUState *c, u32 a, u32 b) {
    if (b == 0) {
        c->lo = 0xFFFFFFFFu;
        c->hi = a;
    } else {
        c->lo = a / b;
        c->hi = a % b;
    }
}

/* --- control flow / system -------------------------------------------------------------- */

void nc_call(CPUState *c, u32 addr);
void nc_jump(CPUState *c, u32 addr);
void nc_poll(CPUState *c);
void nc_syscall(CPUState *c);
void nc_break(CPUState *c, u32 pc, u32 code);
void nc_unimplemented(CPUState *c, u32 pc, u32 word);
u32 nc_mfc0(CPUState *c, int reg);
void nc_mtc0(CPUState *c, int reg, u32 value);
void nc_rfe(CPUState *c);

#define NC_POLL(c, cost)                                                                           \
    do {                                                                                           \
        if (((c)->budget -= (cost)) <= 0) {                                                        \
            nc_poll(c);                                                                            \
        }                                                                                          \
    } while (0)

/* --- GTE (COP2) ------------------------------------------------------------------------- */

u32 gte_read_data(CPUState *c, int reg);
void gte_write_data(CPUState *c, int reg, u32 value);
u32 gte_read_ctrl(CPUState *c, int reg);
void gte_write_ctrl(CPUState *c, int reg, u32 value);
void gte_command(CPUState *c, u32 cmd);

#endif

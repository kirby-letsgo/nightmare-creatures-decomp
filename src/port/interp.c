/* MIPS R3000A interpreter.
 *
 * Runs game code read from the disc at runtime, for any address without a native function (no
 * recompiled code in release builds; functions a decompiled/recompiled build lacks otherwise).
 * Semantics match tools/recomp/recomp.py: branch delay slots, no load delay slots, the same
 * memory/GTE/BIOS helpers, and the address patches and hooks from config/ (via the generated
 * table in nc_addr_patches). */
#include "port/interp.h"

#include "port/runtime.h"

#include <string.h>

/* Cycles charged per instruction (plus an extra cost for loads from RAM), approximating the
 * R3000A so iteration-count timeouts in the libraries behave as on hardware. */
#define INSN_CYCLES 1
#define LOAD_CYCLES 2

/* --- patches and hooks -------------------------------------------------------------------- */

/* One bit per RAM word: set when a patch or hook applies at that address for the resident code
 * (launcher + active overlay). Checked before every instruction. */
static u8 marked[RAM_SIZE / 4 / 8];
static int marked_module = -100;

static void rebuild_marks(void) {
    NcModule mod = nc_active_module();
    memset(marked, 0, sizeof marked);
    for (unsigned i = 0; i < nc_addr_patch_count; i++) {
        const NcAddrPatch *p = &nc_addr_patches[i];
        if (p->module == NC_PATCH_LAUNCHER || p->module == (int)mod) {
            u32 w = (p->addr & (RAM_SIZE - 1)) >> 2;
            marked[w >> 3] |= (u8)(1u << (w & 7));
        }
    }
    marked_module = (int)mod;
}

static bool is_marked(u32 pc) {
    u32 w = (pc & (RAM_SIZE - 1)) >> 2;
    return (marked[w >> 3] >> (w & 7)) & 1;
}

/* Runs hooks at `pc` and reports whether the instruction there is patched out. */
static bool apply_marks(CPUState *c, u32 pc) {
    NcModule mod = nc_active_module();
    bool skip = false;
    for (unsigned i = 0; i < nc_addr_patch_count; i++) {
        const NcAddrPatch *p = &nc_addr_patches[i];
        if (p->addr != pc || (p->module != NC_PATCH_LAUNCHER && p->module != (int)mod)) {
            continue;
        }
        if (p->hook != NULL) {
            p->hook(c);
        }
        if (p->flag != NULL && *p->flag) {
            skip = true;
        }
    }
    return skip;
}

/* --- instruction execution ---------------------------------------------------------------- */

static inline u32 op_of(u32 w) {
    return w >> 26;
}
static inline u32 rs_of(u32 w) {
    return (w >> 21) & 31;
}
static inline u32 rt_of(u32 w) {
    return (w >> 16) & 31;
}
static inline u32 rd_of(u32 w) {
    return (w >> 11) & 31;
}
static inline s32 simm_of(u32 w) {
    return (s16)(w & 0xFFFF);
}

#define SET(reg, value)                                                                            \
    do {                                                                                           \
        u32 v_ = (value);                                                                          \
        if ((reg) != 0) {                                                                          \
            c->r[(reg)] = v_;                                                                      \
        }                                                                                          \
    } while (0)

static bool is_control(u32 w) {
    u32 op = op_of(w);
    return (op >= 1 && op <= 7) || (op == 0 && ((w & 63) == 8 || (w & 63) == 9));
}

/* Executes a non-control-flow instruction. Returns the cycles it costs. */
static int exec_simple(CPUState *c, u32 pc, u32 w) {
    u32 op = op_of(w), rs = rs_of(w), rt = rt_of(w), rd = rd_of(w);
    u32 a = c->r[rs], b = c->r[rt];
    s32 simm = simm_of(w);
    u32 imm = w & 0xFFFF;
    u32 ea = a + (u32)simm;

    if (w == 0) {
        return INSN_CYCLES;
    }
    if (op == 0) {
        u32 sa = (w >> 6) & 31;
        switch (w & 63) {
        case 0x00:
            SET(rd, b << sa);
            break;
        case 0x02:
            SET(rd, b >> sa);
            break;
        case 0x03:
            SET(rd, (u32)((s32)b >> sa));
            break;
        case 0x04:
            SET(rd, b << (a & 31));
            break;
        case 0x06:
            SET(rd, b >> (a & 31));
            break;
        case 0x07:
            SET(rd, (u32)((s32)b >> (a & 31)));
            break;
        case 0x0C:
            nc_syscall(c);
            break;
        case 0x0D:
            nc_break(c, pc, (w >> 6) & 0xFFFFF);
            break;
        case 0x10:
            SET(rd, c->hi);
            break;
        case 0x11:
            c->hi = a;
            break;
        case 0x12:
            SET(rd, c->lo);
            break;
        case 0x13:
            c->lo = a;
            break;
        case 0x18:
            nc_mult(c, a, b);
            break;
        case 0x19:
            nc_multu(c, a, b);
            break;
        case 0x1A:
            nc_div(c, a, b);
            break;
        case 0x1B:
            nc_divu(c, a, b);
            break;
        case 0x20:
        case 0x21:
            SET(rd, a + b);
            break;
        case 0x22:
        case 0x23:
            SET(rd, a - b);
            break;
        case 0x24:
            SET(rd, a & b);
            break;
        case 0x25:
            SET(rd, a | b);
            break;
        case 0x26:
            SET(rd, a ^ b);
            break;
        case 0x27:
            SET(rd, ~(a | b));
            break;
        case 0x2A:
            SET(rd, (s32)a < (s32)b);
            break;
        case 0x2B:
            SET(rd, a < b);
            break;
        default:
            nc_unimplemented(c, pc, w);
        }
        return INSN_CYCLES;
    }
    switch (op) {
    case 0x08:
    case 0x09:
        SET(rt, a + (u32)simm);
        return INSN_CYCLES;
    case 0x0A:
        SET(rt, (s32)a < simm);
        return INSN_CYCLES;
    case 0x0B:
        SET(rt, a < (u32)simm);
        return INSN_CYCLES;
    case 0x0C:
        SET(rt, a & imm);
        return INSN_CYCLES;
    case 0x0D:
        SET(rt, a | imm);
        return INSN_CYCLES;
    case 0x0E:
        SET(rt, a ^ imm);
        return INSN_CYCLES;
    case 0x0F:
        SET(rt, imm << 16);
        return INSN_CYCLES;
    case 0x10: /* COP0 */
        if (rs == 0x00) {
            SET(rt, nc_mfc0(c, (int)rd));
        } else if (rs == 0x04) {
            nc_mtc0(c, (int)rd, b);
        } else if (rs == 0x10 && (w & 63) == 0x10) {
            nc_rfe(c);
        } else {
            nc_unimplemented(c, pc, w);
        }
        return INSN_CYCLES;
    case 0x12: /* COP2 (GTE) */
        if (rs & 0x10) {
            gte_command(c, w & 0x1FFFFFF);
        } else if (rs == 0x00) {
            SET(rt, gte_read_data(c, (int)rd));
        } else if (rs == 0x02) {
            SET(rt, gte_read_ctrl(c, (int)rd));
        } else if (rs == 0x04) {
            gte_write_data(c, (int)rd, b);
        } else if (rs == 0x06) {
            gte_write_ctrl(c, (int)rd, b);
        } else {
            nc_unimplemented(c, pc, w);
        }
        return INSN_CYCLES;
    case 0x20:
        SET(rt, (u32)(s32)(s8)MEM_R8(ea));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x21:
        SET(rt, (u32)(s32)(s16)MEM_R16(ea));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x22:
        SET(rt, nc_lwl(c, ea, b));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x23:
        SET(rt, MEM_R32(ea));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x24:
        SET(rt, MEM_R8(ea));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x25:
        SET(rt, MEM_R16(ea));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x26:
        SET(rt, nc_lwr(c, ea, b));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x28:
        MEM_W8(ea, (u8)b);
        return INSN_CYCLES;
    case 0x29:
        MEM_W16(ea, (u16)b);
        return INSN_CYCLES;
    case 0x2A:
        nc_swl(c, ea, b);
        return INSN_CYCLES;
    case 0x2B:
        MEM_W32(ea, b);
        return INSN_CYCLES;
    case 0x2E:
        nc_swr(c, ea, b);
        return INSN_CYCLES;
    case 0x32: /* lwc2 */
        gte_write_data(c, (int)rt, MEM_R32(ea));
        return INSN_CYCLES + LOAD_CYCLES;
    case 0x3A: /* swc2 */
        MEM_W32(ea, gte_read_data(c, (int)rt));
        return INSN_CYCLES;
    default:
        nc_unimplemented(c, pc, w);
        return INSN_CYCLES;
    }
}

/* Executes the delay slot at `pc` (with patches/hooks) and returns its cost. */
static int exec_delay(CPUState *c, u32 pc) {
    u32 w = MEM_R32(pc);
    if (is_marked(pc) && apply_marks(c, pc)) {
        return INSN_CYCLES;
    }
    return exec_simple(c, pc, w);
}

/* Calls native code for `target` if there is any; returns false to interpret it instead. */
static bool call_native(CPUState *c, u32 target) {
    if (target == 0xA0 || target == 0xB0 || target == 0xC0) {
        bios_call(c, target);
        return true;
    }
    NcFunc fn = nc_lookup(target);
    if (fn == NULL) {
        return false;
    }
    fn(c);
    return true;
}

void interp_call(CPUState *c, u32 addr) {
    const u32 ret = c->r[31];
    const u32 sp = c->r[29];
    u32 pc = addr;

    for (;;) {
        if (marked_module != (int)nc_active_module()) {
            rebuild_marks();
        }
        /* Returned to our caller (with its stack): done. Not checked on entry: the interrupt
         * handler is entered at a resume address that equals its own $ra. */
        if (pc == ret && c->r[29] == sp && pc != addr) {
            return;
        }
        if (pc == 0xA0 || pc == 0xB0 || pc == 0xC0) {
            /* BIOS call entered by a jump (Psy-Q stubs: jr $t2). */
            bios_call(c, pc);
            pc = c->r[31];
            continue;
        }

        u32 w = MEM_R32(pc);
        bool patched = is_marked(pc) && apply_marks(c, pc);

        if (!is_control(w)) {
            int cost = patched ? INSN_CYCLES : exec_simple(c, pc, w);
            pc += 4;
            NC_POLL(c, cost);
            continue;
        }
        if (patched) {
            /* A patched-out branch/jump: only its delay slot runs. */
            int cost = exec_delay(c, pc + 4);
            pc += 8;
            NC_POLL(c, cost + INSN_CYCLES);
            continue;
        }

        u32 op = op_of(w), rs = rs_of(w), rt = rt_of(w);
        u32 a = c->r[rs], b = c->r[rt];
        u32 next = pc + 8;
        bool link = false, native_ok = false;
        u32 target = 0;

        switch (op) {
        case 0x00:
            target = a; /* jr / jalr: target read before the delay slot */
            if ((w & 63) == 9) {
                if (rd_of(w) != 0) {
                    c->r[rd_of(w)] = pc + 8;
                }
                link = true;
            }
            native_ok = true;
            break;
        case 0x01: { /* bltz/bgez(al) */
            bool taken = (rt & 1) ? ((s32)a >= 0) : ((s32)a < 0);
            if ((rt & 0x1E) == 0x10) {
                c->r[31] = pc + 8;
            }
            target = taken ? pc + 4 + ((u32)simm_of(w) << 2) : pc + 8;
            break;
        }
        case 0x02:
        case 0x03:
            target = ((pc + 4) & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
            if (op == 0x03) {
                c->r[31] = pc + 8;
                link = true;
            }
            native_ok = true;
            break;
        case 0x04:
            target = a == b ? pc + 4 + ((u32)simm_of(w) << 2) : pc + 8;
            break;
        case 0x05:
            target = a != b ? pc + 4 + ((u32)simm_of(w) << 2) : pc + 8;
            break;
        case 0x06:
            target = (s32)a <= 0 ? pc + 4 + ((u32)simm_of(w) << 2) : pc + 8;
            break;
        case 0x07:
            target = (s32)a > 0 ? pc + 4 + ((u32)simm_of(w) << 2) : pc + 8;
            break;
        default:
            nc_unimplemented(c, pc, w);
        }

        int cost = INSN_CYCLES + exec_delay(c, pc + 4);
        NC_POLL(c, cost);

        if (native_ok && target != ret && call_native(c, target)) {
            /* jal/jalr into native code returns here; a jump (tail call) returns to our ra. */
            next = link ? pc + 8 : c->r[31];
            pc = next;
            continue;
        }
        pc = target;
    }
}

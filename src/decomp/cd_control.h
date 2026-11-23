#ifndef NC_DECOMP_CD_CONTROL_H
#define NC_DECOMP_CD_CONTROL_H

#include "decomp/interrupt_control.h"

typedef struct {
    u32 location_table, callback, writer, sync;
} CdControlLayout;

/* CD_cw(command, parameters, result, async). The async flag is in the delay slot. */
static inline void issue_cd_control(CPUState *c, u32 writer, u32 parameters, unsigned result_reg, bool async, u32 resume) {
    set_reg(c, 5, parameters);
    set_reg(c, 6, result_reg ? R(result_reg) : 0);
    R(31) = resume;
    R(7) = async;
    NC_POLL(c, 2);
    nc_call(c, writer);
}

static inline void set_cd_control_callback(CPUState *c, u32 slot, u32 callback) {
    set_reg(c, 1, (slot + 0x8000u) & 0xffff0000u);
    MEM_W32(slot, callback);
    NC_POLL(c, 1);
}

enum CdControlMode { CD_CONTROL, CD_CONTROL_FAST, CD_CONTROL_BLOCKING };

/* The CdControl variants retry the command up to four times. Keep the sync callback
 * disabled for the status and Setloc commands preceding the requested one. */
static inline bool cd_control(CPUState *c, const CdControlLayout *layout, u32 entry,
                              enum CdControlMode mode) {
    const bool fast = mode == CD_CONTROL_FAST;
    const bool blocking = mode == CD_CONTROL_BLOCKING;
    /* Guest loop PCs share the same command-call offsets. */
    const u32 loop = entry + (mode == CD_CONTROL ? 0x58 : 0x50);
    /* CdControl keeps its result pointer in s2. The other saved registers
     * move up one slot compared with CdControlF. */
    const unsigned result_reg = fast ? 0 : 18;
    const unsigned command_byte = fast ? 18 : 19;
    const unsigned command = command_byte + 1;
    const unsigned callback = command + 1;
    const unsigned location_entry = callback + 1;
    const unsigned status = location_entry + 1;
    const unsigned last_saved = blocking ? location_entry : status;
    const unsigned frame = mode == CD_CONTROL ? 56 : 48;
    const unsigned ra_offset = mode == CD_CONTROL ? 48 : 44;
    const unsigned saved_base = fast ? 24 : 28; /* stack slot for command_byte */
    if (c->budget <= (mode == CD_CONTROL ? 21 : 19) * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= frame;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(5);
    c->budget -= 2;
    if (!fast) {
        MEM_W32(R(29) + 24, R(18));
        R(18) = R(6);
        c->budget -= 2;
    }
    MEM_W32(R(29) + saved_base + 4, R(command));
    R(command) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 3;
    c->budget -= 2;
    MEM_W32(R(29) + saved_base, R(command_byte));
    R(command_byte) = R(command) & 0xff;
    R(3) = layout->location_table;
    c->budget -= 4;
    MEM_W32(R(29) + saved_base + 8, R(callback));
    c->budget -= 2;
    R(callback) = MEM_R32(layout->callback);
    R(2) = R(command_byte) << 2;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + saved_base + 12, R(location_entry));
    R(location_entry) = R(2) + R(3);
    c->budget -= 2;
    if (!blocking) {
        MEM_W32(R(29) + saved_base + 16, R(status));
        R(status) = 0;
        c->budget -= 2;
    }
    MEM_W32(R(29) + ra_offset, R(31));
    c->budget--;

    for (;;) {
        set_cd_control_callback(c, layout->callback, 0);
        set_reg(c, 2, 1);
        bool check_status = R(command_byte) != R(2);
        NC_POLL(c, 2);
        if (check_status) {
            set_reg(c, 2, (layout->callback + 16 + 0x8000u) & 0xffff0000u);
            R(2) = MEM_R8(layout->callback + 16);
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) & 0x10);
            bool shell_open = R(2) != 0;
            R(4) = 1;
            NC_POLL(c, 2);
            if (shell_open)
                issue_cd_control(c, layout->writer, 0, 0, false, loop + 0x3c);
        }

        bool has_parameters = R(17) != 0;
        NC_POLL(c, 2);
        bool location_failed = false;
        if (has_parameters) {
            load_word(c, 2, R(location_entry));
            NC_POLL(c, 1);
            bool needs_location = R(2) != 0;
            R(4) = 2;
            NC_POLL(c, 2);
            if (needs_location) {
                issue_cd_control(c, layout->writer, R(17), result_reg, false, loop + 0x64);
                location_failed = R(2) != 0;
                NC_POLL(c, 2);
            }
        }
        if (!location_failed) {
            set_cd_control_callback(c, layout->callback, R(callback));
            set_reg(c, 4, R(command) & 0xff);
            issue_cd_control(c, layout->writer, R(17), result_reg, fast, loop + 0x88);
            bool success = R(2) == 0;
            R(2) = blocking ? 0 : R(status) + 1;
            NC_POLL(c, 2);
            if (success)
                break;
        }

        set_reg(c, 16, R(16) - 1);
        set_reg(c, 2, UINT32_MAX);
        bool retry = R(16) != R(2);
        NC_POLL(c, 2);
        if (!retry) {
            set_cd_control_callback(c, layout->callback, R(callback));
            if (!blocking) {
                set_reg(c, status, UINT32_MAX);
                set_reg(c, 2, R(status) + 1);
            }
            break;
        }
    }

    if (blocking) {
        bool issued = R(2) == 0;
        R(4) = 0;
        NC_POLL(c, 2);
        if (issued) {
            R(31) = entry + 0x108;
            R(5) = R(result_reg);
            NC_POLL(c, 2);
            nc_call(c, layout->sync); /* CD_sync(0, result) */
            set_reg(c, 2, R(2) ^ 2);
            R(2) = R(2) == 0; /* CdlComplete */
            NC_POLL(c, 2);
        } else {
            set_reg(c, 2, 0);
        }
    }
    load_word(c, 31, R(29) + ra_offset);
    for (unsigned reg = last_saved; reg >= 16; reg--)
        load_word(c, reg, R(29) + 16 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + frame);
    NC_POLL(c, 2);
    return true;
}

#endif

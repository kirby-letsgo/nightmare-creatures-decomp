#ifndef NC_DECOMP_CD_COMMAND_H
#define NC_DECOMP_CD_COMMAND_H

#include "decomp/cd_wait.h"

static inline void store_cd_command_word(CPUState *c, u32 address, unsigned reg) {
    set_reg(c, 1, cd_wait_hi(address));
    MEM_W32(address, R(reg));
    NC_POLL(c, 1);
}

static void load_cd_command_table(CPUState *c, unsigned reg, unsigned offset_reg, u32 table) {
    set_reg(c, 1, cd_wait_hi(table));
    set_reg(c, 1, R(1) + R(offset_reg));
    load_word(c, reg, R(1) + (s16)table);
}

static void print_cd_command(CPUState *c, const CdWaitLayout *layout, unsigned index_reg, u32 format, u32 resume) {
    load_cd_command_table(c, 5, index_reg, layout->command_names);
    set_reg(c, 4, cd_wait_hi(format));
    set_reg(c, 4, format);
    R(31) = resume;
    NC_POLL(c, 2);
    nc_call(c, layout->print);
}

static void write_cd_command_ports(CPUState *c, const CdWaitLayout *layout) {
    R(31) = layout->writer + 0xcc;
    R(5) = 0;
    NC_POLL(c, 2);
    nc_call(c, layout->sync); /* CD_sync(0, NULL) */
    set_reg(c, 3, R(16) & 0xff);
    set_reg(c, 2, 2);
    bool set_location = R(3) == R(2);
    R(2) = 14;
    NC_POLL(c, 2);
    if (set_location) {
        set_reg(c, 4, 0);
        set_reg(c, 3, R(17));
        bool more;
        do {
            R(2) = MEM_R8(R(3));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, 1, cd_wait_hi(layout->command_names - 8));
            set_reg(c, 1, R(1) + R(4));
            MEM_W8(R(1) + (s16)(layout->command_names - 8), (u8)R(2));
            NC_POLL(c, 1);
            set_reg(c, 4, R(4) + 1);
            set_reg(c, 2, (s32)R(4) < 4);
            more = R(2) != 0;
            R(3)++;
            NC_POLL(c, 2);
        } while (more);
        set_reg(c, 3, R(16) & 0xff);
        set_reg(c, 2, 14);
    }
    bool set_mode = R(3) == R(2);
    R(4) = R(3) << 2;
    NC_POLL(c, 2);
    if (set_mode) {
        R(2) = MEM_R8(R(17));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 1, cd_wait_hi(layout->command_names - 4));
        MEM_W8(layout->command_names - 4, (u8)R(2));
        NC_POLL(c, 1);
    }
    set_reg(c, 2, cd_wait_hi(layout->status));
    set_reg(c, 2, layout->status);
    MEM_W8(R(2), 0);
    NC_POLL(c, 1);
    load_cd_command_table(c, 2, 4, layout->command_names + 0x120);
    set_reg(c, 3, cd_wait_hi(layout->command_names + 0x120));
    set_reg(c, 3, layout->command_names + 0x120);
    bool clear_ready = R(2) != 0;
    NC_POLL(c, 2);
    if (clear_ready) {
        set_reg(c, 1, cd_wait_hi(layout->status + 1));
        MEM_W8(layout->status + 1, 0);
        NC_POLL(c, 1);
    }
    cd_flush_port(c, layout->ports);
    MEM_W8(R(2), 0); /* select bank zero */
    NC_POLL(c, 1);
    set_reg(c, 2, R(3) + 0x100);
    set_reg(c, 3, R(4) + R(2));
    load_word(c, 2, R(3));
    NC_POLL(c, 1);
    bool more = (s32)R(2) > 0;
    R(4) = 0;
    NC_POLL(c, 2);
    if (more) {
        set_reg(c, 6, R(3));
        set_reg(c, 5, R(17));
    }
    while (more) {
        load_cd_port(c, 3, layout->ports + 8);
        R(2) = MEM_R8(R(5));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 5, R(5) + 1);
        MEM_W8(R(3), (u8)R(2)); /* parameter FIFO */
        NC_POLL(c, 1);
        load_word(c, 2, R(6));
        set_reg(c, 4, R(4) + 1);
        set_reg(c, 2, (s32)R(4) < (s32)R(2));
        more = R(2) != 0;
        NC_POLL(c, 2);
    }
    load_cd_port(c, 2, layout->ports + 4);
    set_reg(c, 1, cd_wait_hi(layout->command_names - 3));
    MEM_W8(layout->command_names - 3, (u8)R(16));
    NC_POLL(c, 1);
    MEM_W8(R(2), (u8)R(16));
    NC_POLL(c, 1);
}

static void wait_cd_command_ack(CPUState *c, const CdWaitLayout *layout) {
    const u32 entry = layout->writer;
    const CdWaitSite site = {entry + 0x21c, entry + 0x26c, entry + 0x2c0, entry + 0x2c8, 18, 19, false};
    R(31) = layout->writer + 0x1cc;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2);
    nc_call(c, layout->vsync);
    set_reg(c, 2, R(2) + 960);
    set_reg(c, 4, cd_wait_hi(layout->status));
    set_reg(c, 4, layout->status);
    store_cd_command_word(c, layout->deadline, 2);
    store_cd_command_word(c, layout->deadline + 4, 0);
    R(3) = MEM_R8(R(4));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 2, cd_wait_hi(layout->command_name));
    set_reg(c, 2, layout->command_name);
    store_cd_command_word(c, layout->deadline + 8, 2);
    bool pending = !R(3);
    R(6) = R(21);
    NC_POLL(c, 2);
    if (pending) {
        set_reg(c, 19, cd_wait_hi(layout->command_names + 0x80));
        set_reg(c, 19, layout->command_names + 0x80);
        set_reg(c, 18, R(4));
        set_reg(c, 20, R(18) + 1);
    }
    while (pending) {
        if (cd_wait_expired(c, layout, &site))
            return;
        R(31) = layout->writer + 0x2e4;
        NC_POLL(c, 2);
        nc_call(c, layout->check_callback);
        bool drain = R(2) != 0;
        NC_POLL(c, 2);
        if (drain)
            drain_cd_sync_events(c, layout, layout->writer + 0x30c, 20);
        R(2) = MEM_R8(R(18));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        pending = !R(2);
        R(6) = R(21);
        NC_POLL(c, 2);
    }
    set_reg(c, 4, cd_wait_hi(layout->results));
    set_reg(c, 4, layout->results);
    bool copy = R(6) != 0;
    NC_POLL(c, 2);
    if (copy) {
        set_reg(c, 3, 7);
        copy_cd_response(c, 6, 5);
    }
    set_reg(c, 2, cd_wait_hi(layout->status));
    set_reg(c, 2, layout->status);
    R(2) = MEM_R8(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) ^ 5);
    set_reg(c, 2, R(2) == 0);
    set_reg(c, 2, 0u - R(2)); /* disk error */
}

/* CD_cw writes a command and its parameters, optionally waiting for acknowledgement. */
static inline bool write_cd_command(CPUState *c, const CdWaitLayout *layout) {
    if (c->budget <= 1)
        return false;
    load_cd_port(c, 2, layout->callbacks + 12);
    set_reg(c, 29, R(29) - 72);
    const unsigned saved[] = {17, 21, 18, 16};
    const unsigned args[] = {5, 6, 7, 4};
    for (unsigned i = 0; i < 4; i++) {
        unsigned reg = saved[i];
        MEM_W32(R(29) + 40 + 4 * (reg - 16), R(reg));
        NC_POLL(c, 1);
        set_reg(c, reg, R(args[i]));
    }
    MEM_W32(R(29) + 64, R(31));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 56, R(20));
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(2) < 2);
    bool verbose = !R(2);
    MEM_W32(R(29) + 52, R(19));
    NC_POLL(c, 2);
    if (verbose) {
        set_reg(c, 2, R(16) & 0xff);
        set_reg(c, 2, R(2) << 2);
        print_cd_command(c, layout, 2, layout->verbose_format, layout->writer + 0x64);
    }
    set_reg(c, 2, R(16) & 0xff);
    set_reg(c, 3, R(2) << 2);
    load_cd_command_table(c, 2, 3, layout->command_names + 0x220);
    NC_POLL(c, 1);
    bool needs_params = R(2) != 0;
    R(4) = 0;
    NC_POLL(c, 2);
    bool missing = false;
    if (needs_params) {
        missing = !R(17);
        NC_POLL(c, 2);
    }
    if (missing) {
        cd_flush_port(c, layout->callbacks + 12);
        bool report = (s32)R(2) > 0;
        R(2) = (u32)-2;
        NC_POLL(c, 2);
        if (report) {
            print_cd_command(c, layout, 3, layout->missing_format, layout->writer + 0xbc);
            R(2) = (u32)-2;
            NC_POLL(c, 2);
        }
    } else {
        write_cd_command_ports(c, layout);
        bool wait = !R(18);
        R(2) = 0;
        NC_POLL(c, 2);
        if (wait)
            wait_cd_command_ack(c, layout);
    }
    load_word(c, 31, R(29) + 64);
    for (unsigned reg = 21; reg >= 16; reg--)
        load_word(c, reg, R(29) + 40 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 72);
    NC_POLL(c, 2);
    return true;
}

#endif

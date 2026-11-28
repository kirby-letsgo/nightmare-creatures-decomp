#ifndef NC_DECOMP_CD_WAIT_H
#define NC_DECOMP_CD_WAIT_H

#include "decomp/cd_flush.h"
#include "decomp/cd_sector_dma.h"

typedef struct {
    u32 deadline, command_names, status;
    u32 timeout_message, report_format;
    u32 vsync, puts, print, flush;
    u32 dma_control, dma_name;
    u32 ports, callbacks, results, getintr, check_callback;
    u32 sync_name, ready_name;
    u32 error_message, error_format, unknown_message, unknown_format, irq_table;
    u32 writer, sync, verbose_format, missing_format, command_name;
} CdWaitLayout;

static inline u32 cd_wait_hi(u32 address) {
    return (address + 0x8000u) & 0xffff0000u;
}

typedef struct {
    u32 vsync_return, puts_return, printf_return, flush_return;
    unsigned status_reg, names_reg;
    bool dma;
} CdWaitSite;

static void report_cd_wait_timeout(CPUState *c, const CdWaitLayout *layout,
                                   const CdWaitSite *site) {
    set_reg(c, 4, cd_wait_hi(layout->timeout_message));
    set_reg(c, 4, layout->timeout_message);
    R(31) = site->puts_return;
    NC_POLL(c, 2);
    nc_call(c, layout->puts); /* puts */
    R(4) = MEM_R8(R(site->status_reg));
    NC_POLL(c, NC_LOAD_CYCLES);
    R(2) = MEM_R8(R(site->status_reg) + 1);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 5, cd_wait_hi(layout->deadline + 8));
    load_word(c, 5, layout->deadline + 8);
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 2, R(2) + R(site->names_reg));
    set_reg(c, 4, R(4) << 2);
    load_word(c, 3, R(2)); /* second interrupt name, passed on the stack */
    set_reg(c, 2, cd_wait_hi(layout->command_names - 3));
    R(2) = MEM_R8(layout->command_names - 3);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 4, R(4) + R(site->names_reg));
    set_reg(c, 2, R(2) << 2);
    if (site->dma)
        set_reg(c, 2, R(2) + R(19));
    MEM_W32(R(29) + 16, R(3));
    NC_POLL(c, 1);
    if (site->dma) {
        load_word(c, 6, R(2));
    } else {
        set_reg(c, 1, cd_wait_hi(layout->command_names));
        set_reg(c, 1, R(1) + R(2));
        load_word(c, 6, R(1) + (s16)layout->command_names);
    }
    load_word(c, 7, R(4)); /* first interrupt name */
    set_reg(c, 4, cd_wait_hi(layout->report_format));
    set_reg(c, 4, layout->report_format);
    R(31) = site->printf_return;
    NC_POLL(c, 2);
    nc_call(c, layout->print); /* printf */
    R(31) = site->flush_return;
    NC_POLL(c, 2);
    nc_call(c, layout->flush); /* CD_flush */
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
}

static bool cd_wait_expired(CPUState *c, const CdWaitLayout *layout, const CdWaitSite *site) {
    R(31) = site->vsync_return;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2);
    nc_call(c, layout->vsync); /* VSync(-1) */
    set_reg(c, 3, cd_wait_hi(layout->deadline));
    load_word(c, 3, layout->deadline);
    NC_POLL(c, 1);
    set_reg(c, 3, (s32)R(3) < (s32)R(2));
    bool expired = R(3) != 0;
    NC_POLL(c, 2);
    if (!expired) {
        set_reg(c, 3, cd_wait_hi(layout->deadline + 4));
        load_word(c, 3, layout->deadline + 4);
        NC_POLL(c, 1);
        set_reg(c, 2, R(3) + 1);
        set_reg(c, 1, cd_wait_hi(layout->deadline + 4));
        MEM_W32(layout->deadline + 4, R(2));
        NC_POLL(c, 1);
        set_reg(c, 2, 0x3c0000);
        set_reg(c, 2, (s32)R(2) < (s32)R(3));
        expired = R(2) != 0;
        NC_POLL(c, 2);
    }
    if (expired)
        report_cd_wait_timeout(c, layout, site);
    else
        set_reg(c, 2, 0);
    expired = R(2) != 0;
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
    return expired;
}

static void start_cd_wait(CPUState *c, const CdWaitLayout *layout, u32 operation_name) {
    set_reg(c, 2, R(2) + 960);
    set_reg(c, 1, cd_wait_hi(layout->deadline));
    MEM_W32(layout->deadline, R(2)); /* frame deadline */
    NC_POLL(c, 1);
    set_reg(c, 2, cd_wait_hi(operation_name));
    set_reg(c, 2, operation_name);
    set_reg(c, 1, cd_wait_hi(layout->deadline + 4));
    MEM_W32(layout->deadline + 4, 0); /* poll count */
    NC_POLL(c, 1);
    set_reg(c, 1, cd_wait_hi(layout->deadline + 8));
    MEM_W32(layout->deadline + 8, R(2));
    NC_POLL(c, 1);
}

static inline bool sync_cd_dma(CPUState *c, const CdWaitLayout *layout, const CdWaitSite *site) {
    if (c->budget <= 9)
        return false;
    R(29) -= 48;
    c->budget--;
    MEM_W32(R(29) + 32, R(18));
    R(18) = R(4);
    R(4) = UINT32_MAX;
    c->budget -= 3;
    MEM_W32(R(29) + 40, R(31));
    c->budget--;
    MEM_W32(R(29) + 36, R(19));
    c->budget--;
    MEM_W32(R(29) + 28, R(17));
    c->budget--;
    R(31) = site->vsync_return - 0x44;
    MEM_W32(R(29) + 24, R(16));
    c->budget -= 2;
    nc_call(c, layout->vsync);
    set_reg(c, 19, cd_wait_hi(layout->command_names));
    set_reg(c, 19, layout->command_names);
    set_reg(c, 17, cd_wait_hi(layout->status));
    set_reg(c, 17, layout->status);
    set_reg(c, 16, cd_wait_hi(layout->command_names + 0x80));
    set_reg(c, 16, layout->command_names + 0x80);
    start_cd_wait(c, layout, layout->dma_name);
    while (!cd_wait_expired(c, layout, site)) {
        cd_flush_port(c, layout->dma_control);
        load_word(c, 2, R(2));
        set_reg(c, 3, 1u << 24);
        set_reg(c, 2, R(2) & R(3));
        bool busy = R(2) != 0;
        R(2) = 0;
        NC_POLL(c, 2);
        if (!busy)
            break;
        bool blocking = R(18) == 0;
        R(2) = 1;
        NC_POLL(c, 2);
        if (!blocking)
            break;
    }
    load_word(c, 31, R(29) + 40);
    for (unsigned reg = 19; reg >= 16; reg--)
        load_word(c, reg, R(29) + 24 + (reg - 16) * 4);
    set_reg(c, 29, R(29) + 48);
    NC_POLL(c, 2);
    return true;
}

/* CheckCallback requests synchronous draining when the IRQ dispatcher is
 * already active. Preserve the selected bank across all user callbacks. */
static void drain_cd_sync_events(CPUState *c, const CdWaitLayout *layout, u32 interrupt_return,
                                 unsigned ready_reg) {
    cd_flush_port(c, layout->ports);
    R(2) = MEM_R8(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 17, R(2) & 3);
    for (;;) {
        R(31) = interrupt_return;
        NC_POLL(c, 2);
        nc_call(c, layout->getintr); /* getintr */
        set_reg(c, 16, R(2));
        bool pending = R(16) != 0;
        R(2) = R(16) & 4;
        NC_POLL(c, 2);
        if (!pending)
            break;
        bool ready = R(2) != 0;
        R(2) = R(16) & 2;
        NC_POLL(c, 2);
        if (ready) {
            load_cd_port(c, 2, layout->callbacks + 4);
            NC_POLL(c, 1);
            bool registered = R(2) != 0;
            NC_POLL(c, 2);
            if (registered) {
                R(4) = MEM_R8(R(ready_reg));
                NC_POLL(c, NC_LOAD_CYCLES);
                set_reg(c, 5, cd_wait_hi(layout->results + 8));
                set_reg(c, 5, layout->results + 8);
                u32 handler = R(2);
                R(31) = interrupt_return + 0x3c;
                NC_POLL(c, 2);
                nc_call(c, handler);
            }
            set_reg(c, 2, R(16) & 2);
        }
        bool sync = R(2) != 0;
        NC_POLL(c, 2);
        if (!sync)
            continue;
        load_cd_port(c, 2, layout->callbacks);
        NC_POLL(c, 1);
        bool registered = R(2) != 0;
        NC_POLL(c, 2);
        if (registered) {
            R(4) = MEM_R8(R(18));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, 5, cd_wait_hi(layout->results));
            set_reg(c, 5, layout->results);
            u32 handler = R(2);
            R(31) = interrupt_return + 0x70;
            NC_POLL(c, 2);
            nc_call(c, handler);
            NC_POLL(c, 2);
        }
    }
    cd_flush_port(c, layout->ports);
    MEM_W8(R(2), (u8)R(17));
    NC_POLL(c, 1);
}

static void copy_cd_response(CPUState *c, unsigned dest, unsigned stop) {
    set_reg(c, stop, UINT32_MAX);
    bool more;
    do {
        R(2) = MEM_R8(R(4));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 4, R(4) + 1);
        set_reg(c, 3, R(3) - 1);
        MEM_W8(R(dest), (u8)R(2));
        NC_POLL(c, 1);
        more = R(3) != R(stop);
        R(dest)++;
        NC_POLL(c, 2);
    } while (more);
}

static void finish_cd_sync(CPUState *c, const CdWaitLayout *layout) {
    set_reg(c, 2, 2);
    MEM_W8(R(18), (u8)R(2)); /* acknowledge completion or disk error */
    NC_POLL(c, 1);
    set_reg(c, 5, R(22));
    set_reg(c, 4, cd_wait_hi(layout->results));
    set_reg(c, 4, layout->results);
    bool copy = R(5) != 0;
    R(3) = 7;
    NC_POLL(c, 2);
    if (copy)
        copy_cd_response(c, 5, 7);
    R(2) = R(6);
    NC_POLL(c, 2);
}

static inline bool sync_cd_command(CPUState *c, const CdWaitLayout *layout, u32 entry) {
    const CdWaitSite site = {entry + 0x74, entry + 0xc4, entry + 0x118, entry + 0x120,
                             18,           19,           false};
    if (c->budget <= 11)
        return false;
    R(29) -= 56;
    c->budget--;
    MEM_W32(R(29) + 44, R(21));
    R(21) = R(4); /* zero waits, nonzero polls */
    c->budget -= 2;
    MEM_W32(R(29) + 48, R(22));
    R(22) = R(5); /* optional response buffer */
    R(4) = UINT32_MAX;
    c->budget -= 3;
    MEM_W32(R(29) + 52, R(31));
    c->budget--;
    for (unsigned reg = 20; reg >= 17; reg--) {
        MEM_W32(R(29) + 24 + (reg - 16) * 4, R(reg));
        c->budget--;
    }
    R(31) = entry + 0x34;
    MEM_W32(R(29) + 24, R(16));
    NC_POLL(c, 2);
    nc_call(c, layout->vsync); /* VSync(-1) */
    set_reg(c, 19, cd_wait_hi(layout->command_names + 0x80));
    set_reg(c, 19, layout->command_names + 0x80);
    set_reg(c, 18, cd_wait_hi(layout->status));
    set_reg(c, 18, layout->status);
    set_reg(c, 20, R(18) + 1);
    start_cd_wait(c, layout, layout->sync_name);
    while (!cd_wait_expired(c, layout, &site)) {
        R(31) = entry + 0x13c;
        NC_POLL(c, 2);
        nc_call(c, layout->check_callback); /* CheckCallback */
        bool drain = R(2) != 0;
        NC_POLL(c, 2);
        if (drain)
            drain_cd_sync_events(c, layout, entry + 0x164, 20);
        R(2) = MEM_R8(R(18));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        set_reg(c, 6, R(2) & 0xff);
        set_reg(c, 2, 2);
        bool complete = R(6) == R(2);
        R(2) = 5;
        NC_POLL(c, 2);
        if (!complete) {
            complete = R(6) == R(2);
            NC_POLL(c, 2);
        }
        if (complete) {
            finish_cd_sync(c, layout);
            break;
        }
        bool blocking = R(21) == 0;
        R(2) = 0;
        NC_POLL(c, 2);
        if (!blocking)
            break;
    }
    load_word(c, 31, R(29) + 52);
    for (unsigned reg = 22; reg >= 16; reg--)
        load_word(c, reg, R(29) + 24 + (reg - 16) * 4);
    set_reg(c, 29, R(29) + 56);
    NC_POLL(c, 2);
    return true;
}

static bool take_cd_ready_event(CPUState *c, const CdWaitLayout *layout, bool buffered) {
    u32 address = buffered ? R(19) : R(19) - 1;
    R(2) = MEM_R8(address);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 6, R(2) & 0xff);
    bool pending = R(6) != 0;
    NC_POLL(c, 2);
    if (!pending)
        return false;

    MEM_W8(buffered ? R(19) : R(19) - 1, 0);
    NC_POLL(c, 1);
    bool copy;
    if (buffered) {
        set_reg(c, 4, cd_wait_hi(layout->results + 16));
        set_reg(c, 4, layout->results + 16);
        copy = R(20) != 0;
        R(5) = R(20);
        NC_POLL(c, 2);
        if (copy)
            set_reg(c, 3, 7);
    } else {
        set_reg(c, 5, R(20));
        set_reg(c, 4, cd_wait_hi(layout->results + 8));
        set_reg(c, 4, layout->results + 8);
        copy = R(5) != 0;
        R(3) = 7;
        NC_POLL(c, 2);
    }
    if (copy)
        copy_cd_response(c, 5, 7);
    R(2) = R(6);
    NC_POLL(c, 2);
    return true;
}

static inline bool wait_cd_ready(CPUState *c, const CdWaitLayout *layout, u32 entry) {
    const CdWaitSite site = {entry + 0x7c, entry + 0xcc, entry + 0x120, entry + 0x128,
                             18,           21,           false};
    if (c->budget <= 12)
        return false;
    R(29) -= 64;
    c->budget--;
    MEM_W32(R(29) + 52, R(23));
    R(23) = R(4); /* zero waits, nonzero polls */
    c->budget -= 2;
    MEM_W32(R(29) + 40, R(20));
    R(20) = R(5); /* optional response buffer */
    R(4) = UINT32_MAX;
    c->budget -= 3;
    MEM_W32(R(29) + 56, R(31));
    c->budget--;
    MEM_W32(R(29) + 48, R(22));
    c->budget--;
    MEM_W32(R(29) + 44, R(21));
    c->budget--;
    for (unsigned reg = 19; reg >= 17; reg--) {
        MEM_W32(R(29) + 24 + (reg - 16) * 4, R(reg));
        c->budget--;
    }
    R(31) = entry + 0x38;
    MEM_W32(R(29) + 24, R(16));
    NC_POLL(c, 2);
    nc_call(c, layout->vsync); /* VSync(-1) */
    set_reg(c, 21, cd_wait_hi(layout->command_names + 0x80));
    set_reg(c, 21, layout->command_names + 0x80);
    set_reg(c, 18, cd_wait_hi(layout->status));
    set_reg(c, 18, layout->status);
    set_reg(c, 22, R(18) + 1);
    set_reg(c, 19, R(18) + 2);
    start_cd_wait(c, layout, layout->ready_name);
    while (!cd_wait_expired(c, layout, &site)) {
        R(31) = entry + 0x144;
        NC_POLL(c, 2);
        nc_call(c, layout->check_callback); /* CheckCallback */
        bool drain = R(2) != 0;
        NC_POLL(c, 2);
        if (drain)
            drain_cd_sync_events(c, layout, entry + 0x16c, 22);
        /* The buffered response takes precedence when both slots are full. */
        if (take_cd_ready_event(c, layout, true) || take_cd_ready_event(c, layout, false))
            break;
        bool blocking = R(23) == 0;
        R(2) = 0;
        NC_POLL(c, 2);
        if (!blocking)
            break;
    }
    load_word(c, 31, R(29) + 56);
    for (unsigned reg = 23; reg >= 16; reg--)
        load_word(c, reg, R(29) + 24 + (reg - 16) * 4);
    set_reg(c, 29, R(29) + 64);
    NC_POLL(c, 2);
    return true;
}

#endif

#ifndef NC_DECOMP_CD_WAIT_H
#define NC_DECOMP_CD_WAIT_H

#include "decomp/cd_flush.h"

typedef struct {
    u32 deadline, command_names, status;
    u32 timeout_message, report_format;
    u32 vsync, puts, print, flush;
    u32 dma_control, dma_name;
} CdWaitLayout;

static inline u32 cd_wait_hi(u32 address) {
    return (address + 0x8000u) & 0xffff0000u;
}

typedef struct {
    u32 vsync_return, puts_return, printf_return, flush_return;
    unsigned status_reg, names_reg;
    bool dma;
} CdWaitSite;

static void report_cd_wait_timeout(CPUState *c, const CdWaitLayout *layout, const CdWaitSite *site) {
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

#endif

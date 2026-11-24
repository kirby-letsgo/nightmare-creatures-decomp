#ifndef NC_DECOMP_CD_READ_RETRY_H
#define NC_DECOMP_CD_READ_RETRY_H

#include "decomp/cd_init.h"

typedef struct {
    u32 entry, state, set_sync, set_ready, status, vsync;
    u32 lid_message, puts, fast, retry_message, control;
    u32 last_pos, flush, mode, pos_to_int, callback, get_sector, mismatch_message;
} CdReadRetryLayout;

static void cd_read_call(CPUState *c, u32 target, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2);
    nc_call(c, target);
}

static void handle_open_cd_lid(CPUState *c, const CdReadRetryLayout *layout) {
    R(31) = layout->entry + 0x40;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2);
    nc_call(c, layout->vsync);
    set_reg(c, 2, R(2) & 63);
    bool report = R(2) == 0;
    R(4) = 1;
    NC_POLL(c, 2);
    if (report) {
        cd_call_with_pointer(c, layout->lid_message, layout->puts, layout->entry + 0x5c);
        set_reg(c, 4, 1);
    }
    R(31) = layout->entry + 0x68;
    R(5) = 0;
    NC_POLL(c, 2);
    nc_call(c, layout->fast); /* CdlNop */
    R(31) = layout->entry + 0x70;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2);
    nc_call(c, layout->vsync);
    set_reg(c, 3, (layout->state + 28 + 0x8000u) & 0xffff0000u);
    set_reg(c, 3, layout->state + 28);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    set_reg(c, 2, UINT32_MAX);
    set_reg(c, 1, (layout->state + 20 + 0x8000u) & 0xffff0000u);
    MEM_W32(layout->state + 20, R(2));
    NC_POLL(c, 1);
    NC_POLL(c, 2);
}

static void retry_cd_control(CPUState *c, u32 control, u32 resume) {
    R(31) = resume;
    R(6) = 0;
    NC_POLL(c, 2);
    nc_call(c, control);
}

static bool restore_cd_read_position(CPUState *c, const CdReadRetryLayout *layout) {
    cd_call_with_pointer(c, layout->retry_message, layout->puts, layout->entry + 0xa8);
    set_reg(c, 4, 9); /* CdlPause */
    set_reg(c, 5, 0);
    retry_cd_control(c, layout->control, layout->entry + 0xb8);
    cd_read_call(c, layout->last_pos, layout->entry + 0xc0); /* CdLastPos */
    set_reg(c, 4, 2); /* CdlSetloc */
    set_reg(c, 5, R(2));
    retry_cd_control(c, layout->control, layout->entry + 0xd0);
    bool success = R(2) != 0;
    R(3) = UINT32_MAX;
    NC_POLL(c, 2);
    if (!success) {
        set_reg(c, 2, (layout->state + 20 + 0x8000u) & 0xffff0000u);
        set_reg(c, 2, layout->state + 20);
        MEM_W32(R(2), R(3));
        NC_POLL(c, 1);
        load_word(c, 2, R(2));
        NC_POLL(c, 2);
    }
    return success;
}

static void start_cd_read_attempt(CPUState *c, const CdReadRetryLayout *layout) {
    cd_read_call(c, layout->flush, layout->entry + 0xf8); /* CdFlush */
    set_reg(c, 2, (layout->state + 12 + 0x8000u) & 0xffff0000u);
    set_reg(c, 2, layout->state + 12);
    load_word(c, 16, R(2));
    NC_POLL(c, 1);
    MEM_W8(R(29) + 16, (u8)R(16));
    NC_POLL(c, 1);
    R(31) = layout->entry + 0x114;
    R(16) &= 0xff;
    NC_POLL(c, 2);
    nc_call(c, layout->mode);
    bool set_mode = R(16) != R(2);
    R(4) = 14;
    NC_POLL(c, 2);
    if (!set_mode) {
        set_mode = R(17) != 0;
        NC_POLL(c, 2);
    }
    if (set_mode) {
        set_reg(c, 5, R(29) + 16);
        retry_cd_control(c, layout->control, layout->entry + 0x130);
        bool success = R(2) != 0;
        R(2) = UINT32_MAX;
        NC_POLL(c, 2);
        if (!success) {
            set_reg(c, 1, (layout->state + 20 + 0x8000u) & 0xffff0000u);
            MEM_W32(layout->state + 20, R(2));
            NC_POLL(c, 1);
            NC_POLL(c, 2);
            return;
        }
    }
    cd_read_call(c, layout->last_pos, layout->entry + 0x150);
    R(31) = layout->entry + 0x158;
    R(4) = R(2);
    NC_POLL(c, 2);
    nc_call(c, layout->pos_to_int); /* CdPosToInt */
    set_reg(c, 4, (layout->callback + 0x8000u) & 0xffff0000u);
    set_reg(c, 4, layout->callback); /* cb_read */
    set_reg(c, 3, (layout->state + 32 + 0x8000u) & 0xffff0000u);
    set_reg(c, 3, layout->state + 32);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    cd_read_call(c, layout->set_ready, layout->entry + 0x174);
    set_reg(c, 4, 6); /* CdlReadN */
    set_reg(c, 2, (layout->state + 4 + 0x8000u) & 0xffff0000u);
    load_word(c, 2, layout->state + 4);
    set_reg(c, 5, 0);
    set_reg(c, 1, (layout->state + 8 + 0x8000u) & 0xffff0000u);
    MEM_W32(layout->state + 8, R(2));
    NC_POLL(c, 1);
    cd_read_call(c, layout->fast, layout->entry + 0x194);
    set_reg(c, 2, (layout->state + 0x8000u) & 0xffff0000u);
    load_word(c, 2, layout->state);
    set_reg(c, 4, UINT32_MAX);
    set_reg(c, 1, (layout->state + 20 + 0x8000u) & 0xffff0000u);
    MEM_W32(layout->state + 20, R(2));
    NC_POLL(c, 1);
    cd_read_call(c, layout->vsync, layout->entry + 0x1b0);
    set_reg(c, 1, (layout->state + 24 + 0x8000u) & 0xffff0000u);
    MEM_W32(layout->state + 24, R(2));
    NC_POLL(c, 1);
}

static inline bool cd_read_retry(CPUState *c, const CdReadRetryLayout *layout) {
    if (c->budget <= 7)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 28, R(17));
    R(17) = R(4);
    R(4) = 0;
    c->budget -= 3;
    MEM_W32(R(29) + 32, R(31));
    c->budget--;
    R(31) = layout->entry + 0x1c;
    MEM_W32(R(29) + 24, R(16));
    c->budget -= 2;
    nc_call(c, layout->set_sync); /* detach sync and ready callbacks */
    R(31) = layout->entry + 0x24;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, layout->set_ready);
    cd_read_call(c, layout->status, layout->entry + 0x2c);
    set_reg(c, 2, R(2) & 0x10);
    bool lid_open = R(2) != 0;
    NC_POLL(c, 2);
    bool load_result = true;
    if (lid_open) {
        handle_open_cd_lid(c, layout);
    } else {
        bool retry = R(17) != 0;
        NC_POLL(c, 2);
        if (retry)
            load_result = restore_cd_read_position(c, layout);
        if (load_result)
            start_cd_read_attempt(c, layout);
    }
    if (load_result) {
        set_reg(c, 2, (layout->state + 20 + 0x8000u) & 0xffff0000u);
        load_word(c, 2, layout->state + 20);
    }
    load_word(c, 31, R(29) + 32);
    load_word(c, 17, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

static void load_cd_read_word(CPUState *c, unsigned reg, u32 address) {
    set_reg(c, reg, (address + 0x8000u) & 0xffff0000u);
    load_word(c, reg, address);
}

static void advance_cd_read_word(CPUState *c, u32 address, u32 increment) {
    load_cd_read_word(c, 2, address);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) + increment);
    set_reg(c, 1, (address + 0x8000u) & 0xffff0000u);
    MEM_W32(address, R(2));
    NC_POLL(c, 1);
    load_cd_read_word(c, 2, address);
}

static void collect_cd_read_sector(CPUState *c, const CdReadRetryLayout *layout) {
    load_cd_read_word(c, 3, layout->state + 16);
    set_reg(c, 2, 512);
    bool check_position = R(3) == R(2);
    R(4) = R(29) + 16;
    NC_POLL(c, 2);
    if (check_position) {
        R(31) = layout->callback + 0x54;
        R(5) = 3;
        NC_POLL(c, 2);
        nc_call(c, layout->get_sector); /* read the sector header */
        R(31) = layout->callback + 0x5c;
        R(4) = R(29) + 16;
        NC_POLL(c, 2);
        nc_call(c, layout->pos_to_int); /* CdPosToInt */
        load_cd_read_word(c, 3, layout->state + 32);
        NC_POLL(c, 1);
        bool mismatch = R(2) != R(3);
        NC_POLL(c, 2);
        if (mismatch) {
            cd_call_with_pointer(c, layout->mismatch_message, layout->puts, layout->callback + 0x80);
            set_reg(c, 2, UINT32_MAX);
            MEM_W32(R(16), R(2));
            NC_POLL(c, 1);
        }
    }
    /* The driver consumes the payload even after a header mismatch. */
    set_reg(c, 16, (layout->state + 8 + 0x8000u) & 0xffff0000u);
    set_reg(c, 16, layout->state + 8);
    load_word(c, 4, R(16));
    load_cd_read_word(c, 5, layout->state + 16);
    cd_read_call(c, layout->get_sector, layout->callback + 0xa4);
    load_cd_read_word(c, 2, layout->state + 16);
    load_word(c, 3, R(16));
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 2, R(2) + R(3));
    MEM_W32(R(16), R(2));
    NC_POLL(c, 1);
    advance_cd_read_word(c, layout->state + 20, UINT32_MAX);
    advance_cd_read_word(c, layout->state + 32, 1);
    NC_POLL(c, 2);
}

static bool cd_callback_deadline_passed(CPUState *c, const CdReadRetryLayout *layout, u32 resume) {
    R(31) = resume;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2);
    nc_call(c, layout->vsync);
    load_cd_read_word(c, 3, layout->state + 28);
    NC_POLL(c, 1);
    set_reg(c, 3, R(3) + 1200);
    set_reg(c, 3, (s32)R(3) < (s32)R(2));
    return R(3) != 0;
}

static void finish_cd_read_callback(CPUState *c, const CdReadRetryLayout *layout) {
    load_cd_read_word(c, 4, layout->state + 36);
    cd_read_call(c, layout->set_sync, layout->callback + 0x1b8);
    load_cd_read_word(c, 4, layout->state + 40);
    cd_read_call(c, layout->set_ready, layout->callback + 0x1c8);
    set_reg(c, 4, 9); /* CdlPause */
    set_reg(c, 5, 0);
    retry_cd_control(c, layout->control, layout->callback + 0x1d8);
    load_cd_read_word(c, 3, layout->state - 4);
    NC_POLL(c, 1);
    bool notify = R(3) != 0;
    NC_POLL(c, 2);
    if (notify) {
        load_cd_read_word(c, 2, layout->state + 20);
        NC_POLL(c, 1);
        bool complete = R(2) == 0;
        R(4) = 5; /* CdlDiskError */
        NC_POLL(c, 2);
        if (complete)
            set_reg(c, 4, 2); /* CdlComplete */
        u32 callback = R(3);
        R(31) = layout->callback + 0x20c;
        R(5) = R(17);
        NC_POLL(c, 2);
        nc_call(c, callback);
    }
}

static inline bool handle_cd_read(CPUState *c, const CdReadRetryLayout *layout) {
    if (c->budget <= 8)
        return false;
    R(29) -= 48;
    c->budget--;
    MEM_W32(R(29) + 36, R(17));
    R(17) = R(5);
    R(4) &= 0xff;
    R(2) = 1;
    c->budget -= 4;
    MEM_W32(R(29) + 40, R(31));
    c->budget--;
    bool data_ready = R(4) == R(2);
    MEM_W32(R(29) + 32, R(16));
    c->budget -= 2;
    if (data_ready) {
        set_reg(c, 16, (layout->state + 20 + 0x8000u) & 0xffff0000u);
        set_reg(c, 16, layout->state + 20);
        load_word(c, 2, R(16));
        NC_POLL(c, 1);
        bool remaining = (s32)R(2) > 0;
        NC_POLL(c, 2);
        if (remaining)
            collect_cd_read_sector(c, layout);
    } else {
        set_reg(c, 3, (layout->state + 20 + 0x8000u) & 0xffff0000u);
        set_reg(c, 3, layout->state + 20);
        set_reg(c, 2, UINT32_MAX);
        MEM_W32(R(3), R(2));
        NC_POLL(c, 1);
    }
    R(31) = layout->callback + 0x11c;
    R(4) = UINT32_MAX;
    NC_POLL(c, 2);
    nc_call(c, layout->vsync);
    set_reg(c, 3, (layout->state + 24 + 0x8000u) & 0xffff0000u);
    set_reg(c, 3, layout->state + 24);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    load_cd_read_word(c, 2, layout->state + 20);
    NC_POLL(c, 1);
    bool retry = (s32)R(2) < 0;
    NC_POLL(c, 2);
    if (retry) {
        R(31) = layout->callback + 0x144;
        R(4) = 1;
        NC_POLL(c, 2);
        nc_call(c, layout->entry);
    }
    bool expired = cd_callback_deadline_passed(c, layout, layout->callback + 0x14c);
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
    if (expired) {
        set_reg(c, 1, (layout->state + 20 + 0x8000u) & 0xffff0000u);
        MEM_W32(layout->state + 20, R(2));
        NC_POLL(c, 1);
    }
    load_cd_read_word(c, 2, layout->state + 20);
    NC_POLL(c, 1);
    bool finish = R(2) == 0;
    NC_POLL(c, 2);
    if (!finish) {
        finish = cd_callback_deadline_passed(c, layout, layout->callback + 0x18c);
        NC_POLL(c, 2);
    }
    if (finish)
        finish_cd_read_callback(c, layout);
    load_word(c, 31, R(29) + 40);
    load_word(c, 17, R(29) + 36);
    load_word(c, 16, R(29) + 32);
    set_reg(c, 29, R(29) + 48);
    NC_POLL(c, 2);
    return true;
}

#endif

#ifndef NC_DECOMP_SPU_CONTROL_H
#define NC_DECOMP_SPU_CONTROL_H

/* libspu setup and teardown (_SpuInit, SpuStart, SpuQuit, _SpuDataCallback) and the libsnd
 * wrappers around them, as linked by PSX.EXE and PSX2.EXE. */

#include "decomp/cd_state.h"
#include "decomp/spu_driver.h"

typedef struct {
    u32 init, start, quit;                         /* _SpuInit, SpuStart, SpuQuit */
    u32 data_callback, dma_complete;               /* _SpuDataCallback, _spu_FiDMA */
    u32 reset_callback, driver_init, set_register; /* ResetCallback, _spu_init, _spu_FsetRXX */
    u32 enter_critical, exit_critical;
    u32 open_event, close_event, enable_event, disable_event;
    u32 dma_callback; /* DMACallback */
    u32 started;      /* SpuStart has run */
    u32 event;        /* the SPU DMA completion event */
    u32 callbacks[2]; /* the transfer and IRQ callbacks SpuQuit clears */
    u32 voice_table;  /* last of 24 halfwords set to 0xc000 by a full initialization */
    u32 reverb_mode;  /* value written to SPU register 0xd1 */
    u32 reverb_copy;  /* ... and kept here */
    /* Globals _SpuInit clears in order; entries 3 and 4 of the first group are halfwords. */
    u32 cleared[7], cleared_after[9];
} SpuControlLayout;

static inline void spu_store_zero(CPUState *c, u32 address, bool halfword) {
    set_reg(c, 1, spu_hi(address));
    if (halfword)
        MEM_W16(address, 0);
    else
        MEM_W32(address, 0);
    NC_POLL(c, 1);
}

static inline void spu_control_call(CPUState *c, u32 target, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2);
    nc_call(c, target);
}

static inline bool leave_spu_control(CPUState *c) {
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* A leaf frame around one call with a constant first argument (SsUtReverbOn/Off). */
static inline bool spu_call_with_argument(CPUState *c, u32 entry, u32 target, u32 argument) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    MEM_W32(R(29) + 16, R(31));
    R(31) = entry + 16;
    R(4) = argument;
    c->budget -= 4;
    nc_call(c, target);
    return leave_spu_control(c);
}

/* _SpuDataCallback registers the SPU transfer handler on DMA channel 4. */
static inline bool spu_data_callback(CPUState *c, const SpuControlLayout *layout) {
    return dma_channel_callback(c, 4, layout->dma_callback, layout->data_callback + 20);
}

/* _SpuInit(mode): reset the driver; a zero mode also resets the voice table. */
static inline bool spu_control_init(CPUState *c, const SpuControlLayout *layout) {
    if (c->budget <= 5)
        return false;
    R(29) -= 24;
    MEM_W32(R(29) + 16, R(16));
    MEM_W32(R(29) + 20, R(31));
    R(31) = layout->init + 20;
    R(16) = R(4);
    c->budget -= 5;
    nc_call(c, layout->reset_callback);
    R(31) = layout->init + 28;
    R(4) = R(16);
    NC_POLL(c, 2);
    nc_call(c, layout->driver_init);
    bool keep_voices = R(16) != 0;
    R(4) = 0xc000;
    NC_POLL(c, 2);
    if (!keep_voices) {
        set_reg(c, 3, 0x17);
        set_reg(c, 2, spu_hi(layout->voice_table));
        set_reg(c, 2, layout->voice_table);
        bool more;
        do {
            MEM_W16(R(2), (u16)R(4));
            NC_POLL(c, 1);
            set_reg(c, 3, R(3) - 1);
            more = (s32)R(3) >= 0;
            R(2) -= 2;
            NC_POLL(c, 2);
        } while (more);
    }
    spu_control_call(c, layout->start, layout->init + 72); /* SpuStart */
    set_reg(c, 4, 0xd1);
    spu_load_global(c, 5, layout->reverb_mode);
    for (unsigned i = 0; i < 7; i++)
        spu_store_zero(c, layout->cleared[i], i == 3 || i == 4);
    spu_store_global(c, layout->reverb_copy, 5);
    R(31) = layout->init + 156;
    R(6) = 0;
    NC_POLL(c, 2);
    nc_call(c, layout->set_register); /* _spu_FsetRXX(0xd1, mode, 0) */
    for (unsigned i = 0; i < 9; i++)
        spu_store_zero(c, layout->cleared_after[i], false);
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* SpuStart: once, install the DMA handler and open and enable its completion event. */
static inline bool spu_control_start(CPUState *c, const SpuControlLayout *layout) {
    if (c->budget <= 7)
        return false;
    R(2) = MEM_R32(layout->started);
    R(29) -= 24;
    MEM_W32(R(29) + 16, R(31));
    c->budget -= 7;
    if (R(2) != 0)
        return leave_spu_control(c);
    set_reg(c, 2, 1);
    spu_store_global(c, layout->started, 2);
    spu_control_call(c, layout->enter_critical, layout->start + 40);
    set_reg(c, 4, spu_hi(layout->dma_complete));
    set_reg(c, 4, layout->dma_complete);
    spu_control_call(c, layout->data_callback, layout->start + 56); /* _SpuDataCallback */
    set_reg(c, 4, 0xf0000000);
    set_reg(c, 4, 0xf0000009);
    set_reg(c, 5, 0x20);
    set_reg(c, 6, 0x2000);
    R(7) = 0;
    spu_control_call(c, layout->open_event, layout->start + 80); /* OpenEvent */
    set_reg(c, 4, R(2));
    spu_store_global(c, layout->event, 4);
    spu_control_call(c, layout->enable_event, layout->start + 100); /* EnableEvent */
    spu_control_call(c, layout->exit_critical, layout->start + 108);
    return leave_spu_control(c);
}

/* SpuQuit: undo SpuStart if it ran. */
static inline bool spu_control_quit(CPUState *c, const SpuControlLayout *layout) {
    if (c->budget <= 8)
        return false;
    R(29) -= 24;
    R(3) = MEM_R32(layout->started);
    R(2) = 1;
    MEM_W32(R(29) + 16, R(31));
    c->budget -= 8;
    if (R(3) != R(2))
        return leave_spu_control(c);
    spu_store_zero(c, layout->started, false);
    spu_control_call(c, layout->enter_critical, layout->quit + 40);
    set_reg(c, 4, 0);
    for (unsigned i = 0; i < 2; i++)
        spu_store_zero(c, layout->callbacks[i], false);
    spu_control_call(c, layout->data_callback, layout->quit + 68); /* _SpuDataCallback(0) */
    spu_load_global(c, 4, layout->event);
    spu_control_call(c, layout->close_event, layout->quit + 84); /* CloseEvent */
    spu_load_global(c, 4, layout->event);
    spu_control_call(c, layout->disable_event, layout->quit + 100); /* DisableEvent */
    spu_control_call(c, layout->exit_critical, layout->quit + 108);
    return leave_spu_control(c);
}

#endif

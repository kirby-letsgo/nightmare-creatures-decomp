#ifndef NC_DECOMP_CD_INIT_H
#define NC_DECOMP_CD_INIT_H

#include "decomp/cd_sector_dma.h"
#include "decomp/interrupt_control.h"

typedef struct {
    u32 entry, reset;
    u32 sync_handler, set_sync;
    u32 ready_handler, set_ready;
    u32 read_handler, set_read;
    u32 message, print;
} CdInitLayout;

/* Materialize a pointer argument before calling an SDK routine. */
static inline void cd_call_with_pointer(CPUState *c, u32 argument, u32 target, u32 resume) {
    set_reg(c, 4, (argument + 0x8000u) & 0xffff0000u);
    set_reg(c, 4, argument);
    R(31) = resume;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

/* Try CdReset(1) up to five times before reporting initialization failure. */
static inline bool cd_init(CPUState *c, const CdInitLayout *layout) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 4;
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    for (;;) {
        R(31) = layout->entry + 0x18;
        R(4) = 1;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, layout->reset);
        set_reg(c, 3, 1);
        const bool ready = R(2) == R(3);
        R(16)--;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (ready) {
            cd_call_with_pointer(c, layout->sync_handler, layout->set_sync, layout->entry + 0x34); /* sync */
            cd_call_with_pointer(c, layout->ready_handler, layout->set_ready, layout->entry + 0x44); /* ready */
            cd_call_with_pointer(c, layout->read_handler, layout->set_read, layout->entry + 0x54); /* read */
            R(2) = 1;
            NC_POLL(c, 2 * NC_INSN_CYCLES);
            break;
        }
        set_reg(c, 2, UINT32_MAX);
        const bool exhausted = R(16) == R(2);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        if (exhausted) {
            cd_call_with_pointer(c, layout->message, layout->print, layout->entry + 0x78); /* printf */
            set_reg(c, 2, 0);
            break;
        }
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

typedef struct {
    u32 entry, message, format;
    u32 ports, callbacks;
    u32 puts, print, reset, install, handler, command, sync;
} CdDriverInitLayout;

static bool cd_irq_pending(CPUState *c, u32 ports, bool first_read) {
    load_cd_port(c, 2, ports + 12);
    NC_POLL(c, 1);
    R(2) = MEM_R8(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) & 7);
    bool pending = R(2) != 0;
    if (first_read)
        R(4) = 1; /* delay slot selects CD register bank one */
    NC_POLL(c, 2);
    return pending;
}

static void write_cd_init_port(CPUState *c, u32 slot, unsigned source) {
    load_cd_port(c, 2, slot);
    NC_POLL(c, 1);
    MEM_W8(R(2), (u8)R(source));
    NC_POLL(c, 1);
}

static void cd_init_command(CPUState *c, u32 command, u32 resume) {
    set_reg(c, 5, 0);
    set_reg(c, 6, 0);
    R(31) = resume;
    R(7) = 0;
    NC_POLL(c, 2);
    nc_call(c, command); /* CD_cw */
}

static bool cd_init_command_failed(CPUState *c) {
    bool failed = R(2) != 0;
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
    return failed;
}

static inline bool initialize_cd_driver(CPUState *c, const CdDriverInitLayout *layout) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    R(4) = layout->message;
    c->budget -= 3;
    MEM_W32(R(29) + 16, R(31));
    c->budget--;
    R(31) = layout->entry + 0x18;
    NC_POLL(c, 2);
    nc_call(c, layout->puts); /* puts */
    set_reg(c, 4, (layout->format + 0x8000u) & 0xffff0000u);
    set_reg(c, 4, layout->format);
    set_reg(c, 5, (layout->ports + 28 + 0x8000u) & 0xffff0000u);
    set_reg(c, 5, layout->ports + 28);
    R(31) = layout->entry + 0x30;
    NC_POLL(c, 2);
    nc_call(c, layout->print); /* printf */

    const u32 status_bytes[] = {layout->callbacks + 0x21, layout->callbacks + 0x20};
    const u32 callback_and_status_words[] = {layout->callbacks + 4, layout->callbacks, layout->callbacks + 20, layout->callbacks + 16};
    for (unsigned i = 0; i < 2; i++) {
        set_reg(c, 1, (status_bytes[i] + 0x8000u) & 0xffff0000u);
        MEM_W8(status_bytes[i], 0);
        NC_POLL(c, 1);
    }
    for (unsigned i = 0; i < 4; i++) {
        set_reg(c, 1, (callback_and_status_words[i] + 0x8000u) & 0xffff0000u);
        MEM_W32(callback_and_status_words[i], 0);
        NC_POLL(c, 1);
    }
    R(31) = layout->entry + 0x68;
    NC_POLL(c, 2);
    nc_call(c, layout->reset); /* ResetCallback */
    set_reg(c, 5, (layout->handler + 0x8000u) & 0xffff0000u);
    set_reg(c, 5, layout->handler);
    R(31) = layout->entry + 0x78;
    R(4) = 2;
    NC_POLL(c, 2);
    nc_call(c, layout->install); /* install CD interrupt handler */

    load_cd_port(c, 3, layout->ports);
    set_reg(c, 2, 1);
    MEM_W8(R(3), (u8)R(2));
    NC_POLL(c, 1);
    if (cd_irq_pending(c, layout->ports, true)) {
        set_reg(c, 3, 7);
        do {
            write_cd_init_port(c, layout->ports, 4); /* bank one */
            write_cd_init_port(c, layout->ports + 12, 3); /* acknowledge */
            write_cd_init_port(c, layout->ports + 8, 3); /* enable interrupts */
        } while (cd_irq_pending(c, layout->ports, false));
    }

    set_reg(c, 4, 1); /* CdlNop */
    set_reg(c, 5, 0);
    set_reg(c, 6, 0);
    set_reg(c, 1, (layout->ports + 26 + 0x8000u) & 0xffff0000u);
    MEM_W8(layout->ports + 26, 0);
    NC_POLL(c, 1);
    set_reg(c, 3, (layout->ports + 26 + 0x8000u) & 0xffff0000u);
    R(3) = MEM_R8(layout->ports + 26);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 2, (layout->ports + 25 + 0x8000u) & 0xffff0000u);
    set_reg(c, 2, layout->ports + 25);
    MEM_W8(R(2), (u8)R(3));
    NC_POLL(c, 1);
    load_cd_port(c, 3, layout->ports);
    set_reg(c, 2, 2);
    set_reg(c, 1, (layout->ports + 24 + 0x8000u) & 0xffff0000u);
    MEM_W8(layout->ports + 24, (u8)R(2));
    NC_POLL(c, 1);
    MEM_W8(R(3), 0);
    NC_POLL(c, 1);
    load_cd_port(c, 2, layout->ports + 12);
    set_reg(c, 7, 0);
    MEM_W8(R(2), 0);
    NC_POLL(c, 1);
    load_cd_port(c, 3, layout->ports + 16);
    set_reg(c, 2, 0x1325);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    R(31) = layout->entry + 0x164;
    NC_POLL(c, 2);
    nc_call(c, layout->command);

    load_cd_port(c, 2, layout->callbacks + 16);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) & 0x10);
    bool shell_open = R(2) != 0;
    R(4) = 1;
    NC_POLL(c, 2);
    if (shell_open)
        cd_init_command(c, layout->command, layout->entry + 0x18c);
    set_reg(c, 4, 10); /* CdlInit */
    cd_init_command(c, layout->command, layout->entry + 0x1a0);
    if (!cd_init_command_failed(c)) {
        set_reg(c, 4, 12); /* CdlDemute */
        cd_init_command(c, layout->command, layout->entry + 0x1bc);
        if (!cd_init_command_failed(c)) {
            set_reg(c, 4, 0);
            R(31) = layout->entry + 0x1d0;
            R(5) = 0;
            NC_POLL(c, 2);
            nc_call(c, layout->sync); /* CD_sync */
            set_reg(c, 2, R(2) ^ 2);
            set_reg(c, 2, R(2) != 0);
            set_reg(c, 2, 0u - R(2));
        }
    }
    load_word(c, 31, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

#endif

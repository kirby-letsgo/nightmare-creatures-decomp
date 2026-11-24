#ifndef NC_DECOMP_CD_IRQ_H
#define NC_DECOMP_CD_IRQ_H

#include "decomp/cd_command.h"

static void load_cd_byte(CPUState *c, unsigned reg, u32 address) {
    R(reg) = MEM_R8(address);
    NC_POLL(c, NC_LOAD_CYCLES);
}

static void cd_irq_address(CPUState *c, unsigned reg, u32 address) {
    set_reg(c, reg, cd_wait_hi(address));
    set_reg(c, reg, address);
}

static void cd_irq_store_byte(CPUState *c, u32 address, u32 value) {
    MEM_W8(address, (u8)value);
    NC_POLL(c, 1);
}

/* The controller may change interrupt reasons between reads. Keep sampling
 * until two reasons agree, then drain up to eight bytes and pad the reply. */
static void read_cd_irq_response(CPUState *c, const CdWaitLayout *layout) {
    NC_POLL(c, 2);
    for (;;) {
        load_cd_byte(c, 2, R(4));
        load_cd_byte(c, 3, R(29) + 16);
        set_reg(c, 2, R(2) & 7);
        bool stable = R(3) == R(2);
        R(16) = 0;
        NC_POLL(c, 2);
        if (stable)
            break;
        load_cd_byte(c, 2, R(4));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & 7);
        cd_irq_store_byte(c, R(29) + 16, R(2));
    }
    set_reg(c, 3, R(29) + 24);
    for (;;) {
        cd_flush_port(c, layout->ports);
        load_cd_byte(c, 2, R(2));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & 0x20);
        bool available = R(2) != 0;
        R(2) = (s32)R(16) < 8;
        NC_POLL(c, 2);
        if (!available)
            break;
        cd_flush_port(c, layout->ports + 4);
        load_cd_byte(c, 2, R(2));
        set_reg(c, 16, R(16) + 1);
        cd_irq_store_byte(c, R(3), R(2));
        set_reg(c, 2, (s32)R(16) < 8);
        bool more = R(2) != 0;
        R(3)++;
        NC_POLL(c, 2);
        if (!more) {
            set_reg(c, 2, (s32)R(16) < 8);
            break;
        }
    }
    bool pad = R(2) != 0;
    R(2) = R(29) + 24;
    NC_POLL(c, 2);
    if (pad) {
        set_reg(c, 3, R(16) + R(2));
        set_reg(c, 4, R(29) + 32);
        bool more;
        do {
            cd_irq_store_byte(c, R(3), 0);
            set_reg(c, 3, R(3) + 1);
            set_reg(c, 2, (s32)R(3) < (s32)R(4));
            more = R(2) != 0;
            NC_POLL(c, 2);
        } while (more);
    }
    load_cd_port(c, 3, layout->ports);
    set_reg(c, 2, 1);
    cd_irq_store_byte(c, R(3), R(2));
    load_cd_port(c, 2, layout->ports + 12);
    set_reg(c, 3, 7);
    cd_irq_store_byte(c, R(2), R(3));
    cd_flush_port(c, layout->ports + 8);
    cd_irq_store_byte(c, R(2), R(3));
}

static void update_cd_irq_status(CPUState *c, const CdWaitLayout *layout) {
    load_cd_byte(c, 2, R(29) + 16);
    set_reg(c, 3, 3);
    set_reg(c, 2, R(2) & 0xff);
    bool ack = R(2) == R(3);
    NC_POLL(c, 2);
    if (ack) {
        set_reg(c, 2, cd_wait_hi(layout->command_names - 3));
        load_cd_byte(c, 2, layout->command_names - 3);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) << 2);
        load_cd_command_table(c, 2, 2, layout->command_names + 0x1a0);
        NC_POLL(c, 1);
        bool has_status = R(2) != 0;
        NC_POLL(c, 2);
        if (!has_status)
            return;
    }
    cd_flush_port(c, layout->callbacks + 16);
    set_reg(c, 2, R(2) & 0x10);
    bool lid_was_closed = !R(2);
    NC_POLL(c, 2);
    if (lid_was_closed) {
        load_cd_byte(c, 2, R(29) + 24);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & 0x10);
        bool opened = R(2) != 0;
        NC_POLL(c, 2);
        if (opened) {
            cd_flush_port(c, layout->callbacks + 24);
            set_reg(c, 2, R(2) + 1);
            store_cd_command_word(c, layout->callbacks + 24, 2);
        }
    }
    load_cd_byte(c, 2, R(29) + 24);
    load_cd_byte(c, 3, R(29) + 25);
    set_reg(c, 2, R(2) & 0xff);
    set_reg(c, 17, R(2) & 0x1d);
    store_cd_command_word(c, layout->callbacks + 16, 2);
    store_cd_command_word(c, layout->callbacks + 20, 3);
}

static void report_cd_irq_error(CPUState *c, const CdWaitLayout *layout) {
    load_cd_byte(c, 2, R(29) + 16);
    set_reg(c, 3, 5);
    set_reg(c, 2, R(2) & 0xff);
    bool error = R(2) == R(3);
    NC_POLL(c, 2);
    if (!error)
        return;
    cd_irq_address(c, 4, layout->error_message);
    R(31) = layout->getintr + 0x1dc;
    NC_POLL(c, 2);
    nc_call(c, layout->puts);
    cd_flush_port(c, layout->callbacks + 12);
    bool verbose = (s32)R(2) > 0;
    NC_POLL(c, 2);
    if (verbose) {
        set_reg(c, 2, cd_wait_hi(layout->command_names - 3));
        load_cd_byte(c, 2, layout->command_names - 3);
        load_cd_port(c, 6, layout->callbacks + 16);
        load_cd_port(c, 7, layout->callbacks + 20);
        set_reg(c, 2, R(2) << 2);
        load_cd_command_table(c, 5, 2, layout->command_names);
        cd_irq_address(c, 4, layout->error_format);
        R(31) = layout->getintr + 0x228;
        NC_POLL(c, 2);
        nc_call(c, layout->print);
    }
}

/* Each interrupt publishes the padded reply in one or two SDK buffers. */
static void copy_cd_irq_bytes(CPUState *c, unsigned dest, unsigned count) {
    set_reg(c, 6, UINT32_MAX);
    bool more;
    do {
        load_cd_byte(c, 2, R(5));
        set_reg(c, 5, R(5) + 1);
        set_reg(c, count, R(count) - 1);
        cd_irq_store_byte(c, R(dest), R(2));
        more = R(count) != R(6);
        R(dest)++;
        NC_POLL(c, 2);
    } while (more);
}

static void publish_cd_irq_response(CPUState *c, u32 dest) {
    cd_irq_address(c, 3, dest);
    R(5) = R(29) + 24;
    NC_POLL(c, 2);
    set_reg(c, 4, 7);
    copy_cd_irq_bytes(c, 3, 4);
}

static void cd_irq_ack(CPUState *c, const CdWaitLayout *layout) {
    bool error = R(17) != 0;
    R(2) = 5;
    NC_POLL(c, 2);
    bool second_ack = false;
    if (!error) {
        set_reg(c, 2, cd_wait_hi(layout->command_names - 3));
        load_cd_byte(c, 2, layout->command_names - 3);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) << 2);
        load_cd_command_table(c, 2, 2, layout->command_names + 0xa0);
        NC_POLL(c, 1);
        second_ack = R(2) != 0;
        R(2) = 3;
        NC_POLL(c, 2);
    }
    cd_irq_address(c, 3, layout->status);
    if (!error && !second_ack)
        set_reg(c, 2, 2);
    cd_irq_store_byte(c, R(3), R(2));
    publish_cd_irq_response(c, layout->results);
    R(2) = second_ack ? 1 : 2;
    NC_POLL(c, 2);
}

static void cd_irq_complete(CPUState *c, const CdWaitLayout *layout) {
    bool error = R(17) != 0;
    R(2) = 2;
    NC_POLL(c, 2);
    if (error)
        set_reg(c, 2, 5);
    set_reg(c, 1, cd_wait_hi(layout->status));
    cd_irq_store_byte(c, layout->status, R(2));
    publish_cd_irq_response(c, layout->results);
    R(2) = 2;
    NC_POLL(c, 2);
}

static void cd_irq_data_ready(CPUState *c, const CdWaitLayout *layout) {
    bool error = R(17) != 0;
    R(2) = 1;
    NC_POLL(c, 2);
    if (error) {
        set_reg(c, 2, 1);
        bool single = R(16) == R(2);
        NC_POLL(c, 2);
        if (single)
            set_reg(c, 17, 0);
        error = R(17) != 0;
        R(2) = 1;
        NC_POLL(c, 2);
        if (error)
            set_reg(c, 2, 5);
    }
    set_reg(c, 1, cd_wait_hi(layout->status + 1));
    cd_irq_store_byte(c, layout->status + 1, R(2));
    publish_cd_irq_response(c, layout->results + 8);
    cd_flush_port(c, layout->ports);
    cd_irq_store_byte(c, R(2), 0);
    load_cd_port(c, 3, layout->ports + 12);
    set_reg(c, 2, 4);
    cd_irq_store_byte(c, R(3), 0);
    NC_POLL(c, 2);
}

static void cd_irq_end_or_error(CPUState *c, const CdWaitLayout *layout, bool error) {
    u32 status = error ? layout->status + 1 : layout->status + 2;
    cd_irq_address(c, 4, error ? layout->results : layout->results + 16);
    set_reg(c, 5, R(29) + 24);
    set_reg(c, 2, error ? 5 : 4);
    set_reg(c, 1, cd_wait_hi(status));
    cd_irq_store_byte(c, status, R(2));
    set_reg(c, 3, cd_wait_hi(status));
    load_cd_byte(c, 3, status);
    cd_irq_address(c, 2, status - 1);
    cd_irq_store_byte(c, R(2), R(3));
    R(3) = 7;
    NC_POLL(c, 2);
    copy_cd_irq_bytes(c, 4, 3);
    publish_cd_irq_response(c, layout->results + 8);
    R(2) = error ? 6 : 4;
    NC_POLL(c, 2);
}

/* getintr returns the SDK event mask for a pending CD controller interrupt. */
static inline bool get_cd_interrupt(CPUState *c, const CdWaitLayout *layout) {
    if (c->budget <= 1)
        return false;
    set_reg(c, 29, R(29) - 48);
    load_cd_port(c, 3, layout->ports);
    set_reg(c, 2, 1);
    MEM_W32(R(29) + 40, R(31));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 36, R(17));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 32, R(16));
    NC_POLL(c, 1);
    cd_irq_store_byte(c, R(3), R(2));
    load_cd_port(c, 4, layout->ports + 12);
    NC_POLL(c, 1);
    load_cd_byte(c, 2, R(4));
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) & 7);
    cd_irq_store_byte(c, R(29) + 16, R(2));
    load_cd_byte(c, 2, R(29) + 16);
    NC_POLL(c, 1);
    bool pending = R(2) != 0;
    R(17) = 0;
    NC_POLL(c, 2);
    if (pending) {
        read_cd_irq_response(c, layout);
        update_cd_irq_status(c, layout);
        report_cd_irq_error(c, layout);
        load_cd_byte(c, 2, R(29) + 16);
        NC_POLL(c, 1);
        set_reg(c, 3, R(2) - 1);
        set_reg(c, 2, R(3) < 5);
        bool known = R(2) != 0;
        R(2) = R(3) << 2;
        NC_POLL(c, 2);
        if (known) {
            load_cd_command_table(c, 2, 2, layout->irq_table);
            NC_POLL(c, 1);
            u32 target = R(2);
            NC_POLL(c, 2);
            switch (target - layout->getintr) {
            case 0x258:
                cd_irq_ack(c, layout);
                break;
            case 0x358:
                cd_irq_complete(c, layout);
                break;
            case 0x3a4:
                cd_irq_data_ready(c, layout);
                break;
            case 0x428:
                cd_irq_end_or_error(c, layout, false);
                break;
            case 0x4b0:
                cd_irq_end_or_error(c, layout, true);
                break;
            default:
                nc_jump(c, target);
                return true;
            }
        } else {
            cd_irq_address(c, 4, layout->unknown_message);
            R(31) = layout->getintr + 0x548;
            NC_POLL(c, 2);
            nc_call(c, layout->puts);
            load_cd_byte(c, 5, R(29) + 16);
            cd_irq_address(c, 4, layout->unknown_format);
            R(31) = layout->getintr + 0x55c;
            NC_POLL(c, 2);
            nc_call(c, layout->print);
            set_reg(c, 2, 0);
        }
    } else {
        set_reg(c, 2, 0);
    }
    load_word(c, 31, R(29) + 40);
    load_word(c, 17, R(29) + 36);
    load_word(c, 16, R(29) + 32);
    set_reg(c, 29, R(29) + 48);
    NC_POLL(c, 2);
    return true;
}

#endif

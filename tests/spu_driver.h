#ifndef NC_TESTS_SPU_DRIVER_H
#define NC_TESTS_SPU_DRIVER_H

#include "credits_compare.h"
#include "jump_context.h"
#include "decomp/spu_driver.h"
#include "port/hw/hw.h"

typedef struct {
    SpuDriverLayout layout;
    u32 delay_register; /* pointer the delay helpers write through */
    bool (*init)(CPUState *), (*write_by_io)(CPUState *), (*transfer)(CPUState *);
} SpuDriverFixture;

static bool check_spu_initialization(const SpuDriverFixture *f) {
    const SpuDriverLayout *d = &f->layout;
    const u32 spu = 0x80062000, dpcr = 0x80062400;
    const struct { u32 mode; u16 status; int budget; } cases[] = {
        {0, 0, 4}, {0, 0xf800, 8192}, {1, 0, 4}, {UINT32_MAX, 1, 8192},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE); /* interpret the FIFO upload and all SPU delays */
    MEM_W32(d->registers, spu);
    MEM_W32(d->dma_priority, dpcr);
    MEM_W32(d->print, 0x03e00008);
    MEM_W32(d->print + 4, 0x27180001); /* count timeout diagnostics in t8 */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(spu), 0xcc, 0x200);
        memset(nc_fast_ptr(d->voice_state - 2), 0xee, 24);
        MEM_W32(dpcr, 0x10203040);
        MEM_W16(spu + 0x1ae, cases[i].status);
        CPUState state = arguments(cases[i].mode, 2, 3, cases[i].budget);
        state.r[24] = 0;
        bool full = cases[i].mode == 0;
        ok = compare(state, d->init, f->init, 3) &&
             nc_cpu.r[2] == 0 && nc_cpu.r[24] == (u32)((cases[i].status & 0x7ff) != 0) &&
             MEM_R32(dpcr) == 0x102b3040 && MEM_R16(spu + 0x1aa) == 0xc000 &&
             MEM_R32(d->initialized) == 1 && MEM_R16(d->transfer_address) == (full ? 0x200 : 0) &&
             MEM_R16(d->voice_state - 2) == 0xeeee && MEM_R16(d->voice_state + 20) == 0xeeee;
        for (unsigned j = 0; ok && j < 10; j++)
            ok = MEM_R16(d->voice_state + 2 * j) == 0;
        for (unsigned j = 0; ok && j < 24; j++) {
            u32 voice = spu + 16 * j;
            ok = MEM_R16(voice) == (full ? 0 : 0xcccc) &&
                 MEM_R16(voice + 4) == (full ? 0x3fff : 0xcccc) &&
                 MEM_R16(voice + 6) == (full ? 0x200 : 0xcccc) &&
                 MEM_R16(voice + 12) == 0xcccc;
        }
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_spu_fifo_write(const SpuDriverFixture *f) {
    const SpuDriverLayout *d = &f->layout;
    const u32 buffer = 0x80061000, ports = buffer + 256, trace = ports + 512;
    const unsigned sizes[] = {0, 1, 63, 64, 65, 130, 2, 0};
    StateIO saved = {0};
    cpu_serialize(&saved);
    spu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof sizes / sizeof sizes[0]; i++) {
        StateIO native = {0}, guest = {0};
        bool timeout = i >= 6;
        for (unsigned run = 0; ok && run < 2; run++) {
            MEM_W32(d->registers, timeout ? ports : 0x1f801c00);
            MEM_W16(d->transfer_address, 0xffff); /* a long transfer wraps sound RAM */
            for (unsigned j = 0; j < 132; j++)
                MEM_W8(buffer + j, (u8)(j + 1));
            spu_write(0x1aa, 0);
            if (timeout) {
                MEM_W16(ports + 0x1ae, i == 6 ? 0x400 : 0);
                MEM_W32(d->wait, 0x03e00008); /* set status in wait delay slot */
                MEM_W32(d->wait + 4, 0xa5090000); /* sh t1,0(t0) */
                MEM_W32(d->print, 0x03e00008);
                MEM_W32(d->print + 4, 0xad450000); /* sw a1,0(t2): timeout label */
            }
            nc_cpu = arguments(buffer, sizes[i], 3, i & 1 ? 2 : 4096);
            nc_cpu.r[8] = ports + 0x1ae;
            nc_cpu.r[9] = i == 6 ? 0x400 : 1;
            nc_cpu.r[10] = trace;
            if (run)
                interp_call(&nc_cpu, d->write_by_io);
            else
                ok = f->write_by_io(&nc_cpu);
            StateIO *result = run ? &guest : &native;
            cpu_serialize(result);
            spu_serialize(result);
            if (timeout) {
                ok = ok && MEM_R32(trace) == (i == 6 ? d->write_label : d->restore_label);
            } else {
                u8 data[132];
                spu_write(0x1a6, 0xffff);
                spu_dma_read(data, sizeof data);
                unsigned bytes = (sizes[i] + 1) & ~1u;
                ok = ok && !memcmp(data, nc_fast_ptr(buffer), bytes);
            }
            saved.loading = true;
            saved.pos = 0;
            cpu_serialize(&saved);
            spu_serialize(&saved);
        }
        ok = ok && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_spu_transfer_command(const SpuDriverFixture *f) {
    const SpuDriverLayout *d = &f->layout;
    const u32 spu = 0x80060000, dma = 0x80060400, buffer = 0x80061018;
    const struct { s32 command; u32 direction, size; bool ready; } cases[] = {
        {-1, 5, 0, true}, {4, 5, 0, true}, {2, 5, 0, true},
        {0, 5, 0, true}, {0, 5, 0, false},
        {1, 5, 0, true}, {1, 5, 0, false},
        {3, 1, 0, true}, {3, 0, 64, true}, {3, 1, 65, true},
        {3, 5, UINT32_MAX, true}, {3, 1, 64, false}, {3, 0, 64, false},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE); /* interpret the delay-register callees */
    MEM_W32(d->registers, spu);
    MEM_W32(d->dma_address, dma);
    MEM_W32(d->dma_blocks, dma + 4);
    MEM_W32(d->dma_control, dma + 8);
    MEM_W32(f->delay_register, dma + 12);
    MEM_W32(d->address_shift, 35); /* variable shifts use the low five bits */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        s32 cmd = cases[i].command;
        bool ready = cases[i].ready;
        bool reading = cases[i].direction == 1;
        u16 control = 0x8104;
        if (cmd == 3)
            control |= ready ? (reading ? 0x30 : 0x20) : 0;
        MEM_W16(spu + 0x1aa, control);
        MEM_W16(spu + 0x1a6, ready ? 0x5432 : 0x1234);
        MEM_W16(d->transfer_address, 0x5432);
        MEM_W32(d->direction, cases[i].direction);
        memset(nc_fast_ptr(dma), 0xff, 16);
        CPUState state = arguments((u32)cmd, buffer, cases[i].size, i & 1 ? 12 : 4096);
        ok = compare(state, d->transfer, f->transfer, 11) &&
             nc_cpu.r[2] == (ready ? 0u : (u32)-2);
        if (cmd == 0 || cmd == 1) {
            ok = ok && MEM_R32(d->direction) == (u32)(cmd == 0) &&
                 MEM_R16(spu + 0x1aa) == (ready ? (cmd == 0 ? 0x8134 : 0x8124) : control);
        } else if (cmd == 2) {
            ok = ok && MEM_R16(spu + 0x1a6) == (u16)(buffer >> 3) &&
                 MEM_R16(d->transfer_address) == (u16)(buffer >> 3);
        } else if (cmd == 3 && ready) {
            u32 blocks = cases[i].size / 64 + (cases[i].size % 64 != 0);
            ok = ok && MEM_R32(dma) == buffer && MEM_R32(d->dma_count) == blocks &&
                 MEM_R32(dma + 4) == (blocks << 16 | 16) &&
                 MEM_R32(dma + 8) == (reading ? 0x01000200u : 0x01000201u);
        }
    }
    /* Alias the address register to the direction word. The mode store makes
     * the first retry succeed after the initial address comparison fails. */
    MEM_W32(d->registers, d->direction - 0x1a6);
    MEM_W32(d->direction, 99);
    MEM_W16(d->transfer_address, 1);
    CPUState retry = arguments(0, buffer, 64, 12);
    ok = ok && compare(retry, d->transfer, f->transfer, 11) &&
         nc_cpu.r[2] == 0 && nc_cpu.r[3] == 2;
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

#endif

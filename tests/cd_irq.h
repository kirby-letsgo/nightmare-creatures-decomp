#ifndef NC_TESTS_CD_IRQ_H
#define NC_TESTS_CD_IRQ_H

#include "cd_events.h"
#include "cd_sector_dma.h"

static bool check_get_cd_interrupt(const CdEventFixture *f, bool (*routine)(CPUState *)) {
    const u32 slots = f->ports, ports = 0x80061000;
    const u32 replies = f->results, status = f->status;
    const struct {
        u8 reason, response, cmd, mask;
        bool fifo, verbose;
    } cases[] = {
        {0, 0, 0, 0, false, false},
        {1, 0, 0, 4, false, false}, {1, 0x11, 0, 4, true, false},
        {2, 0, 0, 2, false, false}, {2, 0x10, 0, 2, true, false},
        {3, 0, 0, 2, false, false}, {3, 0, 9, 1, false, false},
        {3, 0x11, 1, 2, true, false}, {3, 0x11, 16, 2, true, false},
        {4, 0x20, 0, 4, true, false},
        {5, 0x10, 0, 6, true, true}, {5, 0, 0, 6, false, false},
        {6, 0, 0, 0, false, false}, {7, 0, 0, 0, false, true},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(f->puts, 0x03e00008);
    MEM_W32(f->puts + 4, 0);
    MEM_W32(f->print, 0x03e00008);
    MEM_W32(f->print + 4, 0);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        /* Point the bank select at its own pointer so writing bank one
         * redirects subsequent status reads to a byte with FIFO data ready. */
        MEM_W32(slots, cases[i].fifo ? slots : ports);
        MEM_W8((slots & ~0xffu) | 1, 0x20);
        MEM_W32(slots + 4, ports + 1);
        MEM_W32(slots + 8, ports + 2);
        MEM_W32(slots + 12, ports + 3);
        MEM_W8(ports + 1, cases[i].response);
        MEM_W8(ports + 3, cases[i].reason);
        MEM_W8(f->callbacks + 33, cases[i].cmd);
        MEM_W32(f->callbacks + 16, 0);
        MEM_W32(f->callbacks + 24, 4);
        MEM_W32(f->callbacks + 12, cases[i].verbose);
        MEM_W32(status, 0xcccccccc);
        memset(nc_fast_ptr(replies), 0xcc, 24);
        CPUState state = arguments(0, 0, 0, i & 1 ? 2 : 4096);
        ok = compare(state, f->getintr, routine, 1) &&
             nc_cpu.r[2] == cases[i].mask;
        if (!ok)
            fprintf(stderr, "getintr case %u failed (mask %u)\n", i, nc_cpu.r[2]);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_interrupt_fifo(const CdEventFixture *f, bool (*routine)(CPUState *)) {
    StateIO saved = {0}, native = {0}, guest = {0};
    cd_transfer_state(&saved);
    bool ok = !saved.error;
    for (unsigned run = 0; run < 2 && ok; run++) {
        cdrom_tick(0);
        cdrom_write(0, 0);
        cdrom_write(1, 1); /* Getstat returns one response byte. */
        cdrom_tick(25000);
        MEM_W8(f->callbacks + 33, 1);
        memset(nc_fast_ptr(f->results), 0xcc, 8);
        nc_cpu = arguments(0, 0, 0, 4096);
        if (run)
            interp_call(&nc_cpu, f->getintr);
        else
            ok = routine(&nc_cpu);
        ok = ok && nc_cpu.r[2] == 2 && MEM_R8(f->results) == MEM_R32(f->callbacks + 16) &&
             !(cdrom_read(0) & 0x20) && !(cdrom_read(3) & 7);
        for (unsigned i = 1; ok && i < 8; i++)
            ok = MEM_R8(f->results + i) == 0;
        cd_transfer_state(run ? &guest : &native);
        saved.loading = true;
        saved.pos = 0;
        cd_transfer_state(&saved);
    }
    ok = ok && !saved.error && !native.error && !guest.error &&
         native.len == guest.len && !memcmp(native.buf, guest.buf, guest.len);
    free(saved.buf);
    free(native.buf);
    free(guest.buf);
    return ok;
}

#endif

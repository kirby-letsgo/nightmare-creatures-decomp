#ifndef NC_TESTS_CD_SECTOR_DMA_H
#define NC_TESTS_CD_SECTOR_DMA_H

#include "jump_context.h"
#include "port/disc.h"
#include "port/hw/hw.h"

static void interrupt_start_state(StateIO *io) {
    cpu_serialize(io);
    timers_serialize(io);
    dma_serialize(io);
    irq_serialize(io);
}

static void cd_transfer_state(StateIO *io) {
    interrupt_start_state(io);
    cdrom_serialize(io);
}

static bool compare_cd_sector_transfer(u32 entry, bool (*routine)(CPUState *), bool driver) {
    const int threshold = driver ? 3 * NC_INSN_CYCLES + NC_LOAD_CYCLES : 4;
    const u32 dest = 0x80062000;
    u8 sector[DISC_DATA_SECTOR];
    if (!disc_read_data(16, sector))
        return false;
    StateIO saved = {0};
    cd_transfer_state(&saved);
    bool ok = !saved.error;
    const unsigned counts[] = {1, DISC_DATA_SECTOR / 4};
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0] && ok; i++) {
        StateIO native = {0}, guest = {0};
        for (unsigned run = 0; run < 2 && ok; run++) {
            /* Read the ISO volume descriptor at 00:02:16 (BCD), then let the SDK request its FIFO. */
            cdrom_tick(0);
            cdrom_write(0, 0);
            cdrom_write(2, 0);
            cdrom_write(2, 2);
            cdrom_write(2, 0x16);
            cdrom_write(1, 2); /* Setloc */
            cdrom_write(1, 6); /* ReadN */
            cdrom_tick(150000 + PSX_CPU_HZ / 75);
            memset(nc_fast_ptr(dest - 1), 0xa5, sizeof sector + 2);
            nc_cpu = arguments(dest, counts[i], 3, threshold);
            if (!run && routine(&nc_cpu))
                ok = false;
            nc_cpu.budget = i ? threshold + 1 : 4096;
            if (run)
                interp_call(&nc_cpu, entry);
            else
                ok = routine(&nc_cpu) && ok;
            ok = ok && nc_cpu.r[2] == (driver ? 0u : 1u) && !memcmp(nc_fast_ptr(dest), sector, 4 * counts[i]) &&
                 MEM_R8(dest - 1) == 0xa5 && MEM_R8(dest + 4 * counts[i]) == 0xa5;
            cd_transfer_state(run ? &guest : &native);
            saved.loading = true;
            saved.pos = 0;
            cd_transfer_state(&saved);
        }
        ok = ok && !saved.error && !native.error && !guest.error && native.len == guest.len &&
             !memcmp(native.buf, guest.buf, guest.len);
        free(native.buf);
        free(guest.buf);
    }
    free(saved.buf);
    return ok;
}

#endif

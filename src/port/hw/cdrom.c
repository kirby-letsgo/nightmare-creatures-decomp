#include "port/hw/hw.h"
#include "port/runtime.h"

/* Placeholder CD-ROM controller: logs register traffic. Implemented in Phase 3. */

u8 cdrom_read(u32 reg) {
    NC_LOG("cdrom: read reg %u", reg);
    return 0;
}

void cdrom_write(u32 reg, u8 value) {
    NC_LOG("cdrom: write reg %u = 0x%02X", reg, value);
}

u32 cdrom_dma_read(u8 *dst, u32 bytes) {
    (void)dst;
    NC_LOG("cdrom: dma %u bytes", bytes);
    return 0;
}

void cdrom_tick(u64 cycles) {
    (void)cycles;
}

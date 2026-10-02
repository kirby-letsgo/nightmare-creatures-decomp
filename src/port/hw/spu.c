#include "port/hw/hw.h"

#include <string.h>

/* SPU register file and sound RAM. Audio output is implemented in Phase 3. */

#define SPU_RAM_SIZE 0x80000u

static u16 regs[0x200];
static u8 spu_ram[SPU_RAM_SIZE];
static u32 transfer_addr;

u16 spu_read(u32 reg) {
    u32 idx = (reg >> 1) & 0x1FF;
    if (reg == 0x1AE) { /* SPUSTAT mirrors SPUCNT's low bits; never busy */
        return regs[0x1AA >> 1] & 0x3F;
    }
    return regs[idx];
}

void spu_write(u32 reg, u16 value) {
    regs[(reg >> 1) & 0x1FF] = value;
    if (reg == 0x1A6) { /* transfer start address, in 8-byte units */
        transfer_addr = (u32)value * 8u;
    }
}

void spu_dma_write(const u8 *src, u32 bytes) {
    for (u32 i = 0; i < bytes; i++) {
        spu_ram[(transfer_addr + i) & (SPU_RAM_SIZE - 1)] = src[i];
    }
    transfer_addr = (transfer_addr + bytes) & (SPU_RAM_SIZE - 1);
}

void spu_dma_read(u8 *dst, u32 bytes) {
    for (u32 i = 0; i < bytes; i++) {
        dst[i] = spu_ram[(transfer_addr + i) & (SPU_RAM_SIZE - 1)];
    }
    transfer_addr = (transfer_addr + bytes) & (SPU_RAM_SIZE - 1);
}

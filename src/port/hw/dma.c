#include "port/hw/hw.h"
#include "port/runtime.h"
#include "port/savestate.h"

/* DMA channels: 0 MDECin, 1 MDECout, 2 GPU, 3 CDROM, 4 SPU, 5 PIO, 6 OTC.
 * Transfers complete instantly when started. */

typedef struct Channel {
    u32 madr, bcr, chcr;
} Channel;

static Channel ch[7];
static u32 dpcr = 0x07654321u, dicr;

static void finish(int n) {
    ch[n].chcr &= ~0x01000000u;
    u32 enabled = (dicr >> 16) & 0x7Fu;
    if (enabled & (1u << n)) {
        dicr |= 1u << (24 + n);
    }
    bool master = (dicr & 0x00800000u) != 0;
    bool any = ((dicr >> 24) & enabled) != 0;
    if (dicr & 0x8000u || (master && any)) {
        dicr |= 0x80000000u;
        irq_raise(IRQ_DMA);
    }
}

static void gpu_dma(Channel *c) {
    u32 sync = (c->chcr >> 9) & 3;
    bool to_device = (c->chcr & 1) != 0;
    u32 addr = c->madr & 0x1FFFFCu;
    if (sync == 2) { /* linked list (ordering table) */
        for (int guard = 0; guard < 0x100000; guard++) {
            u32 header = MEM_R32(addr);
            u32 count = header >> 24;
            for (u32 i = 0; i < count; i++) {
                gpu_gp0(MEM_R32(addr + 4 + i * 4));
            }
            if (header & 0x800000u) {
                break; /* end marker 0xFFFFFF */
            }
            addr = header & 0x1FFFFCu;
        }
        return;
    }
    u32 words = sync == 0 ? (c->bcr & 0xFFFF ? c->bcr & 0xFFFF : 0x10000)
                          : (c->bcr & 0xFFFF) * (c->bcr >> 16);
    for (u32 i = 0; i < words; i++, addr = (addr + 4) & 0x1FFFFCu) {
        if (to_device) {
            gpu_gp0(MEM_R32(addr));
        } else {
            MEM_W32(addr, gpu_read());
        }
    }
}

static void otc_dma(Channel *c) {
    u32 addr = c->madr & 0x1FFFFCu;
    u32 words = c->bcr & 0xFFFF ? c->bcr & 0xFFFF : 0x10000;
    for (u32 i = 0; i < words; i++, addr -= 4) {
        MEM_W32(addr, i == words - 1 ? 0x00FFFFFFu : ((addr - 4) & 0x1FFFFFu));
    }
}

static void block_bytes(Channel *c, u32 *addr, u32 *bytes) {
    u32 sync = (c->chcr >> 9) & 3;
    u32 words = sync == 0 ? (c->bcr & 0xFFFF ? c->bcr & 0xFFFF : 0x10000)
                          : (c->bcr & 0xFFFF) * (c->bcr >> 16);
    *addr = c->madr & 0x1FFFFCu;
    *bytes = words * 4;
}

static void start(int n) {
    Channel *c = &ch[n];
    u32 addr, bytes;
    switch (n) {
    case 0:
        block_bytes(c, &addr, &bytes);
        mdec_dma_write(nc_ram + addr, bytes);
        break;
    case 1:
        block_bytes(c, &addr, &bytes);
        mdec_dma_read(nc_ram + addr, bytes);
        break;
    case 2:
        gpu_dma(c);
        break;
    case 3:
        block_bytes(c, &addr, &bytes);
        cdrom_dma_read(nc_ram + addr, bytes);
        break;
    case 4:
        block_bytes(c, &addr, &bytes);
        if (c->chcr & 1) {
            spu_dma_write(nc_ram + addr, bytes);
        } else {
            spu_dma_read(nc_ram + addr, bytes);
        }
        break;
    case 6:
        otc_dma(c);
        break;
    default:
        NC_LOG("dma: channel %d not supported (chcr=0x%08X)", n, c->chcr);
        break;
    }
    finish(n);
}

u32 dma_read(u32 reg) {
    u32 n = (reg >> 4) & 7;
    if (n == 7) {
        return (reg & 0xF) == 0 ? dpcr : dicr;
    }
    switch (reg & 0xF) {
    case 0x0:
        return ch[n].madr;
    case 0x4:
        return ch[n].bcr;
    case 0x8:
        return ch[n].chcr;
    default:
        return 0;
    }
}

void dma_write(u32 reg, u32 value) {
    u32 n = (reg >> 4) & 7;
    if (n == 7) {
        if ((reg & 0xF) == 0) {
            dpcr = value;
        } else {
            /* Flag bits (24-30) are acknowledged by writing 1. */
            u32 flags = (dicr & ~value) & 0x7F000000u;
            dicr = (value & 0x00FF803Fu) | flags;
            if (!(((dicr >> 24) & (dicr >> 16) & 0x7F) && (dicr & 0x00800000u))) {
                dicr &= ~0x80000000u;
            }
        }
        return;
    }
    switch (reg & 0xF) {
    case 0x0:
        ch[n].madr = value & 0xFFFFFFu;
        break;
    case 0x4:
        ch[n].bcr = value;
        break;
    case 0x8:
        ch[n].chcr = value;
        if (value & 0x01000000u) {
            start((int)n);
        }
        break;
    default:
        break;
    }
}

void dma_serialize(StateIO *io) {
    STATE_VAR(io, ch);
    STATE_VAR(io, dpcr);
    STATE_VAR(io, dicr);
}

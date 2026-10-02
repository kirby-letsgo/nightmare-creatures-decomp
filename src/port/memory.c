#include "port/hw/hw.h"
#include "port/runtime.h"

/* Total emulated cycles; advanced by nc_poll (cpu.c). */
extern u64 nc_cycles;

/* IRQ controller, DMA and root counters are 32-bit registers that the libraries also access
 * with byte/halfword loads and stores (e.g. LIBCD's dma_execute read-modify-writes DICR's
 * enable byte). Sub-word accesses select or merge the right lanes of the aligned word. */
static bool is_word_register(u32 phys) {
    return phys >= 0x1F801070u && phys < 0x1F801130u;
}

static u32 dev_read(u32 phys, int size);
static void dev_write(u32 phys, u32 value, int size);

/* Bits that must read as "no change" when merging a partial write: DICR flags acknowledge on 1,
 * I_STAT bits acknowledge on 0. */
static u32 neutral_bits(u32 aligned, u32 current) {
    if (aligned == 0x1F8010F4u) {
        return current & 0x00FFFFFFu;
    }
    if (aligned == 0x1F801070u) {
        return 0xFFFFFFFFu;
    }
    return current;
}

u32 io_read(u32 phys, int size) {
    if (size < 4 && is_word_register(phys)) {
        u32 word = dev_read(phys & ~3u, 4);
        u32 v = word >> ((phys & 3) * 8);
        return size == 1 ? (v & 0xFFu) : (v & 0xFFFFu);
    }
    return dev_read(phys, size);
}

void io_write(u32 phys, u32 value, int size) {
    if (size < 4 && is_word_register(phys)) {
        u32 aligned = phys & ~3u, shift = (phys & 3) * 8;
        u32 mask = (size == 1 ? 0xFFu : 0xFFFFu) << shift;
        u32 merged =
            (neutral_bits(aligned, dev_read(aligned, 4)) & ~mask) | ((value << shift) & mask);
        dev_write(aligned, merged, 4);
        return;
    }
    dev_write(phys, value, size);
}

/* Hardware register space (physical 0x1F801000..) and anything else outside RAM/scratchpad. */
static u32 dev_read(u32 phys, int size) {
    if (phys >= 0x1F801070u && phys < 0x1F801078u) {
        return irq_read((phys >> 2) & 1);
    }
    if (phys >= 0x1F801080u && phys < 0x1F801100u) {
        return dma_read(phys - 0x1F801080u);
    }
    if (phys >= 0x1F801100u && phys < 0x1F801130u) {
        return timers_read(phys - 0x1F801100u, nc_cycles);
    }
    if (phys >= 0x1F801800u && phys < 0x1F801804u) {
        return cdrom_read(phys - 0x1F801800u);
    }
    if (phys == 0x1F801820u || phys == 0x1F801824u) {
        return mdec_read((phys >> 2) & 1);
    }
    if (phys == 0x1F801810u) {
        return gpu_read();
    }
    if (phys == 0x1F801814u) {
        return gpu_status();
    }
    if (phys >= 0x1F801C00u && phys < 0x1F802000u) {
        u32 lo = spu_read(phys - 0x1F801C00u);
        return size == 4 ? lo | (u32)spu_read(phys + 2 - 0x1F801C00u) << 16 : lo;
    }
    if (phys >= 0x1F801000u && phys < 0x1F801070u) {
        return 0; /* memory control, SIO/pad ports */
    }
    if (phys >= 0x1FC00000u && phys < 0x1FC80000u) {
        return 0; /* BIOS ROM is not present */
    }
    NC_LOG("io_read%d unmapped 0x%08X (ra=0x%08X)", size * 8, phys, nc_cpu.r[31]);
    return 0;
}

static void dev_write(u32 phys, u32 value, int size) {
    if (phys >= 0x1F801070u && phys < 0x1F801078u) {
        irq_write((phys >> 2) & 1, value);
    } else if (phys >= 0x1F801080u && phys < 0x1F801100u) {
        dma_write(phys - 0x1F801080u, value);
    } else if (phys >= 0x1F801100u && phys < 0x1F801130u) {
        timers_write(phys - 0x1F801100u, value, nc_cycles);
    } else if (phys >= 0x1F801800u && phys < 0x1F801804u) {
        cdrom_write(phys - 0x1F801800u, (u8)value);
    } else if (phys == 0x1F801820u || phys == 0x1F801824u) {
        mdec_write((phys >> 2) & 1, value);
    } else if (phys == 0x1F801810u) {
        gpu_gp0(value);
    } else if (phys == 0x1F801814u) {
        gpu_gp1(value);
    } else if (phys >= 0x1F801C00u && phys < 0x1F802000u) {
        spu_write(phys - 0x1F801C00u, (u16)value);
        if (size == 4) {
            spu_write(phys + 2 - 0x1F801C00u, (u16)(value >> 16));
        }
    } else if ((phys >= 0x1F801000u && phys < 0x1F801070u) || phys == 0x1FFE0130u) {
        /* memory control, SIO/pad ports, cache control: ignored */
    } else {
        NC_LOG("io_write%d unmapped 0x%08X = 0x%08X", size * 8, phys, value);
    }
}

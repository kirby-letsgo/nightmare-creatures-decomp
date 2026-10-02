#include "port/hw/hw.h"
#include "port/runtime.h"

/* Total emulated cycles; advanced by nc_poll (cpu.c). */
extern u64 nc_cycles;

/* Hardware register space (physical 0x1F801000..) and anything else outside RAM/scratchpad. */
u32 io_read(u32 phys, int size) {
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

void io_write(u32 phys, u32 value, int size) {
    if (phys >= 0x1F801070u && phys < 0x1F801078u) {
        irq_write((phys >> 2) & 1, value);
    } else if (phys >= 0x1F801080u && phys < 0x1F801100u) {
        dma_write(phys - 0x1F801080u, value);
    } else if (phys >= 0x1F801100u && phys < 0x1F801130u) {
        timers_write(phys - 0x1F801100u, value, nc_cycles);
    } else if (phys >= 0x1F801800u && phys < 0x1F801804u) {
        cdrom_write(phys - 0x1F801800u, (u8)value);
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

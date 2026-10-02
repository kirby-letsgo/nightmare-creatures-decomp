/* Emulated PS1 hardware blocks, accessed through io_read/io_write (port/memory.c). */
#ifndef NC_PORT_HW_H
#define NC_PORT_HW_H

#include "port/recomp.h"

#include <stdbool.h>

/* CPU clock and frame timing (NTSC). */
#define PSX_CPU_HZ 33868800u
#define PSX_CYCLES_PER_FRAME (PSX_CPU_HZ / 60u)

/* Interrupt sources (I_STAT / I_MASK bits). */
enum {
    IRQ_VBLANK = 0,
    IRQ_GPU = 1,
    IRQ_CDROM = 2,
    IRQ_DMA = 3,
    IRQ_TIMER0 = 4,
    IRQ_TIMER1 = 5,
    IRQ_TIMER2 = 6,
    IRQ_PAD = 7,
    IRQ_SIO = 8,
    IRQ_SPU = 9,
};

/* irq.c */
void irq_raise(int source);
bool irq_pending(void);
u32 irq_read(u32 reg);
void irq_write(u32 reg, u32 value);

/* timers.c: `cycles` is the total emulated cycle count. */
u32 timers_read(u32 reg, u64 cycles);
void timers_write(u32 reg, u32 value, u64 cycles);

/* dma.c */
u32 dma_read(u32 reg);
void dma_write(u32 reg, u32 value);

/* gpu.c */
void gpu_gp0(u32 word);
void gpu_gp1(u32 word);
u32 gpu_read(void);
u32 gpu_status(void);
void gpu_vblank(void);

/* cdrom.c */
u8 cdrom_read(u32 reg);
void cdrom_write(u32 reg, u8 value);
u32 cdrom_dma_read(u8 *dst, u32 bytes);
void cdrom_tick(u64 cycles);

/* spu.c */
u16 spu_read(u32 reg);
void spu_write(u32 reg, u16 value);
void spu_dma_write(const u8 *src, u32 bytes);
void spu_dma_read(u8 *dst, u32 bytes);
void spu_cdda_feed(const u8 *sector);
void spu_xa_feed(const u8 *sector);

#endif

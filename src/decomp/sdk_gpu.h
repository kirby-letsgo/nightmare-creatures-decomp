#ifndef NC_DECOMP_SDK_GPU_H
#define NC_DECOMP_SDK_GPU_H

/* Psy-Q libgpu: drawing and display environments, image transfers and the GPU command
 * queue. Every executable links the same library code at its own addresses, so each
 * routine takes the executable's layout. Within each region below the library keeps the
 * same relative layout everywhere; offsets are relative to the region's first address. */

#include "decomp/interrupt_control.h"

typedef struct {
    u32 strings; /* the first libgpu diagnostic format */
    u32 code;    /* SetDefDrawEnv, the first libgpu routine */
    u32 data;    /* the current driver table pointer */
    u32 bss;     /* the clear-rectangle packet */
    u32 queue;   /* the last-command record, followed by the command queue */
    u32 memcpy, printf, vsync, reset_callback, dma_callback, set_intr_mask;
} NcGpuLayout;

/* The upper halfword a lui loads for an address, paired with a signed low half. */
static inline u32 gpu_hi(u32 address) {
    return (address + 0x8000) & 0xffff0000;
}

static inline u32 gpu_lo(u32 address) {
    return (u32)(s32)(s16)address;
}

/* _ctl writes a GP1 command through the GPU port pointer at `port_slot` and remembers the
 * command's low byte per opcode in `shadow`; _getctl reads that byte back. */
static inline bool sdk_gpu_control(CPUState *c, u32 port_slot, u32 shadow) {
    if (c->budget <= 12)
        return false;
    c->budget--;
    R(2) = MEM_R32(port_slot);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(2), R(4));
    R(2) = R(4) >> 24; /* GP1 opcode */
    R(1) = gpu_hi(shadow) + R(2);
    c->budget -= 4;
    MEM_W8(R(1) + gpu_lo(shadow), (u8)R(4));
    c->budget -= 3;
    return true;
}

static inline bool sdk_gpu_control_byte(CPUState *c, u32 shadow) {
    if (c->budget <= 7)
        return false;
    R(1) = gpu_hi(shadow) + R(4);
    c->budget -= 2;
    R(2) = MEM_R8(R(1) + gpu_lo(shadow));
    c->budget -= NC_LOAD_CYCLES + 2;
    return true;
}

/* Both GPU layouts encode the draw-mode command, but move the flag bits. */
static u32 draw_mode_word(bool alternate, bool display, bool dither, u32 page, u32 *opcode) {
    *opcode = 0xe1000000u;
    if (dither)
        *opcode |= alternate ? 0x800 : 0x200;
    u32 bits = page & (alternate ? 0x27ff : 0x9ff);
    if (display)
        bits |= alternate ? 0x1000 : 0x400;
    return *opcode | bits;
}

static bool sdk_gpu_draw_tpage(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 60)
        return false;
    R(29) -= 0x28;
    c->budget--;
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 0x18, R(18));
    R(18) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(7);
    R(2) = 1;
    c->budget -= 3;
    MEM_W32(R(29) + 0x20, R(31));
    c->budget--;
    R(31) = L->code + 0x67c;
    MEM_W8(R(19) + 3, 1);
    c->budget -= 2;
    nc_call(c, L->code + 0xdb4);
    R(3) = 1;
    bool alternate = R(2) == 1;
    c->budget -= 3;
    if (!alternate) {
        R(31) = L->code + 0x690;
        c->budget -= 2;
        nc_call(c, L->code + 0xdb4);
        R(3) = 2;
        alternate = R(2) == 2;
        c->budget -= 3;
    }

    bool display = R(18) != 0, dither = R(16) != 0;
    R(2) = draw_mode_word(alternate, display, dither, R(17), &R(alternate ? 3 : 7));
    c->budget -= 5 + alternate + display + dither;
    MEM_W32(R(19) + 4, R(2));
    c->budget--;
    R(31) = MEM_R32(R(29) + 0x20);
    c->budget -= NC_LOAD_CYCLES;
    for (int reg = 19; reg >= 16; reg--) {
        R(reg) = MEM_R32(R(29) + 0x10 + (reg - 16) * 4);
        c->budget -= NC_LOAD_CYCLES;
    }
    R(29) += 0x28;
    c->budget -= 3;
    return true;
}

static bool sdk_gpu_draw_area_packet(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 161)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(5);
    R(2) = 2;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    MEM_W8(R(17) + 3, 2);
    c->budget--;
    R(4) = (u32)(s32)(s16)MEM_R16(R(16));
    c->budget -= NC_LOAD_CYCLES;
    R(5) = (u32)(s32)(s16)MEM_R16(R(16) + 2);
    R(31) = L->code + 0x1ca0;
    c->budget -= NC_LOAD_CYCLES + 2;
    nc_call(c, L->code + 0x20b8);
    MEM_W32(R(17) + 4, R(2));
    c->budget--;

    /* The first command can overlap the rectangle. Reload the fields before
     * computing the inclusive bottom-right corner with 16-bit wraparound. */
    u32 x = MEM_R16(R(16));
    c->budget -= NC_LOAD_CYCLES;
    u32 width = MEM_R16(R(16) + 4);
    c->budget -= NC_LOAD_CYCLES;
    u32 y = MEM_R16(R(16) + 2);
    c->budget -= NC_LOAD_CYCLES + 3;
    u32 height = MEM_R16(R(16) + 6);
    R(4) = (u32)(s32)(s16)(x + width - 1);
    R(5) = (u32)(s32)(s16)(y + height - 1);
    R(31) = L->code + 0x1cd8;
    c->budget -= NC_LOAD_CYCLES + 6;
    nc_call(c, L->code + 0x2184);
    MEM_W32(R(17) + 8, R(2));
    c->budget--;
    R(31) = MEM_R32(R(29) + 24);
    c->budget -= NC_LOAD_CYCLES;
    R(17) = MEM_R32(R(29) + 20);
    c->budget -= NC_LOAD_CYCLES;
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 32;
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

/* Read DMA2 CHCR or GPUSTAT through the driver's port pointer. */
static void read_gpu_sync_bit(CPUState *c, const NcGpuLayout *L, u32 port_slot, u32 mask) {
    set_reg(c, 2, gpu_hi(port_slot));
    load_word(c, 2, port_slot);
    NC_POLL(c, 1);
    load_word(c, 2, R(2));
    set_reg(c, 3, mask);
    set_reg(c, 2, R(2) & R(3));
}

static bool gpu_queue_ready(CPUState *c, const NcGpuLayout *L, bool blocking) {
    read_gpu_sync_bit(c, L, L->data + 0xe8, 1u << 24);
    bool dma_busy = R(2) != 0;
    NC_POLL(c, 2);
    if (dma_busy)
        return false;
    read_gpu_sync_bit(c, L, L->data + 0xdc, 1u << 26);
    bool ready = R(2) != 0;
    if (blocking)
        R(2) = 0; /* delay slot supplies the success result */
    NC_POLL(c, 2);
    return ready;
}

static void gpu_sync_call(CPUState *c, u32 target, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2);
    nc_call(c, target);
}

static bool gpu_sync_timed_out(CPUState *c, const NcGpuLayout *L, u32 resume) {
    gpu_sync_call(c, L->code + 0x361c, resume); /* get_alarm */
    bool expired = R(2) != 0;
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
    return expired;
}

static void drain_gpu_queue(CPUState *c, const NcGpuLayout *L) {
    gpu_sync_call(c, L->code + 0x35e8, L->code + 0x34b8); /* set_alarm */
    NC_POLL(c, 2);
    for (;;) {
        set_reg(c, 3, gpu_hi(L->data + 0x10c));
        load_word(c, 3, L->data + 0x10c);
        set_reg(c, 2, gpu_hi(L->data + 0x110));
        load_word(c, 2, L->data + 0x110);
        NC_POLL(c, 1);
        bool empty = R(3) == R(2);
        NC_POLL(c, 2);
        if (empty)
            break;
        NC_POLL(c, 2);
        gpu_sync_call(c, L->code + 0x3048, L->code + 0x34c8); /* _exeque */
        if (gpu_sync_timed_out(c, L, L->code + 0x34d0))
            return;
    }
    while (!gpu_queue_ready(c, L, true)) {
        if (gpu_sync_timed_out(c, L, L->code + 0x3504))
            return;
    }
    NC_POLL(c, 2);
}

static bool sdk_gpu_sync_gpu_queue(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 4)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    bool blocking = R(4) == 0;
    MEM_W32(R(29) + 16, R(16));
    c->budget -= 2;
    if (blocking) {
        drain_gpu_queue(c, L);
    } else {
        /* Report the original depth even if _exeque advances the queue. */
        set_reg(c, 2, gpu_hi(L->data + 0x10c));
        load_word(c, 2, L->data + 0x10c);
        set_reg(c, 3, gpu_hi(L->data + 0x110));
        load_word(c, 3, L->data + 0x110);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) - R(3));
        set_reg(c, 16, R(2) & 63);
        bool queued = R(16) != 0;
        NC_POLL(c, 2);
        if (queued)
            gpu_sync_call(c, L->code + 0x3048, L->code + 0x3580);
        if (gpu_queue_ready(c, L, false)) {
            set_reg(c, 2, R(16));
        } else {
            bool empty = R(16) == 0;
            R(2) = R(16);
            NC_POLL(c, 2);
            if (empty) {
                R(2) = 1;
                NC_POLL(c, 2);
            }
        }
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

/* The GPU driver addresses these globals through the same upper halfword. */
static void load_gpu_global(CPUState *c, const NcGpuLayout *L, unsigned reg, u32 address) {
    set_reg(c, reg, gpu_hi(address));
    load_word(c, reg, address);
}

static void store_gpu_global(CPUState *c, const NcGpuLayout *L, u32 address, unsigned reg) {
    set_reg(c, 1, gpu_hi(address));
    MEM_W32(address, R(reg));
    NC_POLL(c, 1);
}

static void load_image_halfword(CPUState *c, unsigned reg, u32 address, bool sign_extend) {
    u16 value = MEM_R16(address);
    R(reg) = sign_extend ? (u32)(s32)(s16)value : (u32)value;
    NC_POLL(c, NC_LOAD_CYCLES);
}

/* Both dimensions are bounded by the configured VRAM extent minus one. */
static void clear_rect_limit(CPUState *c, const NcGpuLayout *L, u32 address) {
    set_reg(c, 2, gpu_hi(address));
    set_reg(c, 2, address);
    load_image_halfword(c, 2, R(2), false);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) << 16);
    set_reg(c, 2, (u32)((s32)R(2) >> 16));
    set_reg(c, 4, R(2) - 1);
    set_reg(c, 2, (s32)R(4) < (s32)R(3));
}

static bool prepare_clear_rectangle(CPUState *c, const NcGpuLayout *L) {
    load_image_halfword(c, 3, R(8) + 4, true);
    NC_POLL(c, 1);
    bool negative = (s32)R(3) < 0;
    R(9) = R(5); /* RGB and draw-to-display flag */
    NC_POLL(c, 2);
    if (negative) {
        set_reg(c, 2, 0);
    } else {
        clear_rect_limit(c, L, L->data + 0xc);
        bool clip = R(2) != 0;
        R(2) = R(3);
        NC_POLL(c, 2);
        if (clip) {
            set_reg(c, 3, R(4));
            R(2) = R(3);
            NC_POLL(c, 2);
        }
    }
    load_image_halfword(c, 3, R(8) + 6, true);
    NC_POLL(c, 1);
    negative = (s32)R(3) < 0;
    MEM_W16(R(8) + 4, (u16)R(2));
    NC_POLL(c, 2);
    if (negative) {
        set_reg(c, 3, 0);
    } else {
        clear_rect_limit(c, L, L->data + 0xe);
        bool clip = R(2) != 0;
        NC_POLL(c, 2);
        if (clip) {
            R(3) = R(4);
            NC_POLL(c, 2);
        }
    }
    load_image_halfword(c, 2, R(8), false);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) & 63);
    bool aligned = R(2) == 0;
    MEM_W16(R(8) + 6, (u16)R(3));
    NC_POLL(c, 2);
    if (aligned) {
        load_image_halfword(c, 2, R(8) + 4, false);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & 63);
        aligned = R(2) == 0;
        R(2) = 0x05ff0000;
        NC_POLL(c, 2);
    }
    return aligned;
}

static void build_vram_fill_packet(CPUState *c, const NcGpuLayout *L) {
    set_reg(c, 2, R(2) | 0xffff);
    set_reg(c, 3, 0x00ff0000);
    set_reg(c, 3, R(3) | 0xffff);
    store_gpu_global(c, L, L->bss, 2); /* five GP0 words, end of list */
    set_reg(c, 2, 0xe6000000);
    set_reg(c, 3, R(9) & R(3));
    set_reg(c, 5, 0x02000000); /* fill VRAM */
    store_gpu_global(c, L, L->bss + 0x4, 2);
    load_gpu_global(c, L, 2, L->data + 0xdc);
    set_reg(c, 3, R(3) | R(5));
    load_word(c, 4, R(2));
    set_reg(c, 2, R(9) >> 31);
    set_reg(c, 2, R(2) << 10);
    store_gpu_global(c, L, L->bss + 0xc, 3);
    set_reg(c, 3, 0xe1000000);
    set_reg(c, 2, R(2) | R(3));
    set_reg(c, 4, R(4) & 0x7ff);
    set_reg(c, 4, R(4) | R(2));
    store_gpu_global(c, L, L->bss + 0x8, 4); /* draw mode */
    load_word(c, 2, R(8));
    store_gpu_global(c, L, L->bss + 0x10, 2);
    load_word(c, 2, R(8) + 4);
    store_gpu_global(c, L, L->bss + 0x14, 2);
}

static void read_clear_draw_parameter(CPUState *c, const NcGpuLayout *L, u32 parameter,
                                      u32 resume) {
    R(31) = resume;
    R(4) = parameter;
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x2d10); /* _param */
}

/* Unaligned clears draw a flat rectangle with unrestricted drawing bounds.
 * A second linked packet restores the original draw area and offset. */
static void build_rect_clear_packets(CPUState *c, const NcGpuLayout *L) {
    set_reg(c, 5, 0x00ff0000);
    set_reg(c, 5, R(5) | 0xffff);
    set_reg(c, 4, 0xe4ff0000);
    set_reg(c, 4, R(4) | 0xffff);
    set_reg(c, 7, 0x03ff0000);
    set_reg(c, 6, gpu_hi(L->bss + 0x24));
    set_reg(c, 6, L->bss + 0x24);
    set_reg(c, 2, R(6) & R(5));
    set_reg(c, 3, 0x08000000);
    set_reg(c, 2, R(2) | R(3));
    set_reg(c, 16, 0xe3000000);
    set_reg(c, 17, 0xe5000000);
    store_gpu_global(c, L, L->bss, 2); /* eight words, link to restore packet */
    set_reg(c, 2, 0xe6000000);
    set_reg(c, 5, R(9) & R(5));
    set_reg(c, 3, 0x60000000);
    set_reg(c, 5, R(5) | R(3));
    store_gpu_global(c, L, L->bss + 0x10, 2);
    load_gpu_global(c, L, 2, L->data + 0xdc);
    set_reg(c, 3, 0xe1000000);
    store_gpu_global(c, L, L->bss + 0x4, 16);
    store_gpu_global(c, L, L->bss + 0x8, 4);
    store_gpu_global(c, L, L->bss + 0xc, 17);
    load_word(c, 4, R(2));
    set_reg(c, 2, R(9) >> 31);
    set_reg(c, 2, R(2) << 10);
    set_reg(c, 2, R(2) | R(3));
    store_gpu_global(c, L, L->bss + 0x18, 5); /* rectangle opcode and RGB */
    set_reg(c, 4, R(4) & 0x7ff);
    set_reg(c, 4, R(4) | R(2));
    store_gpu_global(c, L, L->bss + 0x14, 4);
    load_word(c, 2, R(8));
    set_reg(c, 7, R(7) | 0xffff);
    store_gpu_global(c, L, L->bss + 0x1c, 2);
    load_word(c, 2, R(8) + 4);
    MEM_W32(R(6), R(7));
    NC_POLL(c, 1);
    store_gpu_global(c, L, L->bss + 0x20, 2);
    read_clear_draw_parameter(c, L, 3, L->code + 0x2664);
    set_reg(c, 2, R(2) | R(16));
    store_gpu_global(c, L, L->bss + 0x28, 2);
    read_clear_draw_parameter(c, L, 4, L->code + 0x2678);
    set_reg(c, 3, 0xe4000000);
    set_reg(c, 2, R(2) | R(3));
    store_gpu_global(c, L, L->bss + 0x2c, 2);
    read_clear_draw_parameter(c, L, 5, L->code + 0x2690);
    set_reg(c, 2, R(2) | R(17));
    store_gpu_global(c, L, L->bss + 0x30, 2);
    NC_POLL(c, 2);
}

static bool sdk_gpu_clear_gpu_rectangle(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 5)
        return false;
    R(29) -= 32;
    R(8) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    if (prepare_clear_rectangle(c, L))
        build_vram_fill_packet(c, L);
    else
        build_rect_clear_packets(c, L);
    set_reg(c, 4, gpu_hi(L->bss));
    set_reg(c, 4, L->bss);
    gpu_sync_call(c, L->code + 0x2cc4, L->code + 0x272c); /* launch linked-list DMA */
    set_reg(c, 2, 0);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Clamp the dimensions in the caller's RECT, then split the pixel count
 * into packed 32-bit words and 16-word DMA blocks. */
static bool size_gpu_transfer(CPUState *c, const NcGpuLayout *L, bool readback) {
    load_image_halfword(c, 4, R(17) + 4, true);
    if (readback)
        NC_POLL(c, 1);
    else
        set_reg(c, 21, 0);
    bool negative = (s32)R(4) < 0;
    R(3) = R(4);
    NC_POLL(c, 2);
    if (negative) {
        set_reg(c, 3, 0);
    } else {
        set_reg(c, 5, gpu_hi(L->data + 0xc));
        set_reg(c, 5, L->data + 0xc);
        load_image_halfword(c, 2, R(5), false);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) << 16);
        set_reg(c, 2, (u32)((s32)R(2) >> 16));
        set_reg(c, 2, (s32)R(2) < (s32)R(4));
        bool too_wide = R(2) != 0;
        NC_POLL(c, 2);
        if (too_wide) {
            load_image_halfword(c, 3, R(5), false);
            NC_POLL(c, 2);
        }
    }
    load_image_halfword(c, 5, R(17) + 6, true);
    MEM_W16(R(17) + 4, (u16)R(3));
    NC_POLL(c, 1);
    negative = (s32)R(5) < 0;
    R(3) = R(5);
    NC_POLL(c, 2);
    if (negative) {
        set_reg(c, 4, 0);
        set_reg(c, 2, R(4) << 16);
    } else {
        set_reg(c, 6, gpu_hi(L->data + 0xe));
        set_reg(c, 6, L->data + 0xe);
        load_image_halfword(c, 2, R(6), false);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) << 16);
        set_reg(c, 2, (u32)((s32)R(2) >> 16));
        set_reg(c, 2, (s32)R(2) < (s32)R(5));
        bool too_tall = R(2) != 0;
        R(4) = R(3);
        NC_POLL(c, 2);
        if (too_tall) {
            load_image_halfword(c, 4, R(6), false);
            R(2) = R(4) << 16;
            NC_POLL(c, 2);
        } else {
            set_reg(c, 2, R(4) << 16);
        }
    }
    load_image_halfword(c, 3, R(17) + 4, true);
    set_reg(c, 2, (u32)((s32)R(2) >> 16));
    nc_mult(c, R(3), R(2));
    NC_POLL(c, 1);
    MEM_W16(R(17) + 6, (u16)R(4));
    NC_POLL(c, 1);
    set_reg(c, 2, c->lo);
    set_reg(c, 2, R(2) + 1);
    set_reg(c, 3, R(2) >> 31);
    set_reg(c, 2, R(2) + R(3));
    set_reg(c, 4, (u32)((s32)R(2) >> 1));
    bool has_pixels = (s32)R(4) > 0;
    R(16) = (u32)((s32)R(2) >> 5);
    NC_POLL(c, 2);
    return has_pixels;
}

static bool wait_for_gpu_command(CPUState *c, const NcGpuLayout *L, bool readback) {
    set_reg(c, 3, R(16));
    set_reg(c, 2, R(3) << 4);
    set_reg(c, 16, R(4) - R(2)); /* leading words transferred through GP0 */
    load_gpu_global(c, L, 2, L->data + 0xdc);
    set_reg(c, 20, R(3)); /* remaining DMA blocks */
    load_word(c, 2, R(2));
    set_reg(c, 3, 1u << 26);
    set_reg(c, 2, R(2) & R(3));
    bool ready = R(2) != 0;
    if (!readback)
        R(4) = 0xa0000000;
    NC_POLL(c, 2);
    if (!ready)
        set_reg(c, 19, 1u << 26);
    while (!ready) {
        if (gpu_sync_timed_out(c, L, readback ? (L->code + 0x2abc) : (L->code + 0x286c)))
            return false;
        load_gpu_global(c, L, 2, L->data + 0xdc);
        NC_POLL(c, 1);
        load_word(c, 2, R(2));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & R(19));
        ready = R(2) != 0;
        if (!readback)
            R(4) = 0xa0000000;
        NC_POLL(c, 2);
    }
    return true;
}

static void begin_gpu_transfer(CPUState *c, const NcGpuLayout *L, bool readback) {
    load_gpu_global(c, L, 3, L->data + 0xdc);
    set_reg(c, 2, 0x04000000);
    MEM_W32(R(3), R(2)); /* disable GPU DMA */
    NC_POLL(c, 1);
    load_gpu_global(c, L, 3, L->data + 0xd8);
    set_reg(c, 2, 0x01000000);
    MEM_W32(R(3), R(2)); /* clear texture cache */
    NC_POLL(c, 1);
    if (readback) {
        load_gpu_global(c, L, 3, L->data + 0xd8);
        set_reg(c, 2, 0xc0000000);
        MEM_W32(R(3), R(2)); /* VRAM-to-CPU command */
    } else {
        load_gpu_global(c, L, 2, L->data + 0xd8);
        bool alternate_opcode = R(21) != 0;
        NC_POLL(c, 2);
        if (alternate_opcode)
            set_reg(c, 4, 0xb0000000);
        MEM_W32(R(2), R(4)); /* CPU-to-VRAM command */
    }
    NC_POLL(c, 1);
    load_gpu_global(c, L, 3, L->data + 0xd8);
    load_word(c, 2, R(17));
    NC_POLL(c, 1);
    MEM_W32(R(3), R(2)); /* VRAM coordinates */
    NC_POLL(c, 1);
    load_gpu_global(c, L, 3, L->data + 0xd8);
    load_word(c, 2, R(17) + 4);
    if (readback)
        NC_POLL(c, 1);
    else
        set_reg(c, 16, R(16) - 1);
    MEM_W32(R(3), R(2)); /* dimensions */
    NC_POLL(c, 1);
}

static void send_upload_prefix(CPUState *c, const NcGpuLayout *L) {
    set_reg(c, 2, UINT32_MAX);
    bool more = R(16) != R(2);
    NC_POLL(c, 2);
    if (more)
        set_reg(c, 4, UINT32_MAX);
    while (more) {
        load_word(c, 3, R(18));
        set_reg(c, 18, R(18) + 4);
        load_gpu_global(c, L, 2, L->data + 0xd8);
        set_reg(c, 16, R(16) - 1);
        MEM_W32(R(2), R(3));
        NC_POLL(c, 1);
        more = R(16) != R(4);
        NC_POLL(c, 2);
    }
}

static bool receive_readback_prefix(CPUState *c, const NcGpuLayout *L) {
    load_gpu_global(c, L, 2, L->data + 0xdc);
    NC_POLL(c, 1);
    load_word(c, 2, R(2));
    set_reg(c, 3, 1u << 27); /* GPUSTAT ready to send VRAM */
    set_reg(c, 2, R(2) & R(3));
    bool ready = R(2) != 0;
    NC_POLL(c, 2);
    if (!ready)
        set_reg(c, 17, 1u << 27);
    while (!ready) {
        if (gpu_sync_timed_out(c, L, L->code + 0x2b68))
            return false;
        load_gpu_global(c, L, 2, L->data + 0xdc);
        NC_POLL(c, 1);
        load_word(c, 2, R(2));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & R(17));
        ready = R(2) != 0;
        NC_POLL(c, 2);
    }
    set_reg(c, 16, R(16) - 1);
    set_reg(c, 2, UINT32_MAX);
    bool more = R(16) != R(2);
    NC_POLL(c, 2);
    if (more)
        set_reg(c, 3, UINT32_MAX);
    while (more) {
        load_gpu_global(c, L, 2, L->data + 0xd8);
        NC_POLL(c, 1);
        load_word(c, 2, R(2));
        set_reg(c, 16, R(16) - 1);
        MEM_W32(R(18), R(2));
        NC_POLL(c, 1);
        more = R(16) != R(3);
        R(18) += 4;
        NC_POLL(c, 2);
    }
    return true;
}

static void finish_gpu_transfer(CPUState *c, const NcGpuLayout *L, bool readback) {
    bool dma = R(20) != 0;
    R(3) = 0x04000000;
    NC_POLL(c, 2);
    if (dma) {
        load_gpu_global(c, L, 2, L->data + 0xdc);
        set_reg(c, 3, R(3) | (readback ? 3u : 2u));
        MEM_W32(R(2), R(3)); /* GPU DMA direction */
        NC_POLL(c, 1);
        load_gpu_global(c, L, 2, L->data + 0xe0);
        set_reg(c, 4, 0x01000000);
        MEM_W32(R(2), R(18)); /* start after leading words */
        NC_POLL(c, 1);
        set_reg(c, 2, R(20) << 16);
        load_gpu_global(c, L, 3, L->data + 0xe4);
        set_reg(c, 2, R(2) | 16);
        MEM_W32(R(3), R(2));
        NC_POLL(c, 1);
        load_gpu_global(c, L, 2, L->data + 0xe8);
        set_reg(c, 4, R(4) | (readback ? 0x200u : 0x201u));
        MEM_W32(R(2), R(4));
        NC_POLL(c, 1);
    }
    set_reg(c, 2, 0);
}

static bool transfer_gpu_image(CPUState *c, const NcGpuLayout *L, bool readback) {
    const unsigned frame = readback ? 56 : 64;
    const unsigned saved_ra = readback ? 52 : 56;
    const unsigned last_saved = readback ? 20 : 21;
    if (c->budget <= (readback ? 8 : 9))
        return false;
    R(29) -= frame;
    c->budget--;
    MEM_W32(R(29) + 36, R(17));
    R(17) = R(4); /* RECT */
    c->budget -= 2;
    MEM_W32(R(29) + 40, R(18));
    R(18) = R(5); /* pixel data */
    c->budget -= 2;
    MEM_W32(R(29) + saved_ra, R(31));
    c->budget--;
    for (unsigned reg = last_saved; reg >= 19; reg--) {
        MEM_W32(R(29) + 32 + 4 * (reg - 16), R(reg));
        c->budget--;
    }
    R(31) = readback ? (L->code + 0x29c4) : (L->code + 0x2774);
    MEM_W32(R(29) + 32, R(16));
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x35e8); /* set_alarm */
    if (!size_gpu_transfer(c, L, readback)) {
        R(2) = UINT32_MAX;
        NC_POLL(c, 2);
    } else if (wait_for_gpu_command(c, L, readback)) {
        begin_gpu_transfer(c, L, readback);
        if (!readback)
            send_upload_prefix(c, L);
        if (!readback || receive_readback_prefix(c, L))
            finish_gpu_transfer(c, L, readback);
    }
    load_word(c, 31, R(29) + saved_ra);
    for (unsigned reg = last_saved; reg >= 16; reg--)
        load_word(c, reg, R(29) + 32 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + frame);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_upload_gpu_image(CPUState *c, const NcGpuLayout *L) {
    return transfer_gpu_image(c, L, false);
}

static bool sdk_gpu_read_gpu_image(CPUState *c, const NcGpuLayout *L) {
    return transfer_gpu_image(c, L, true);
}

static void report_gpu_timeout(CPUState *c, const NcGpuLayout *L) {
    load_gpu_global(c, L, 3, L->data + 0xdc);
    set_reg(c, 4, gpu_hi(L->strings + 0x2b8));
    set_reg(c, 4, L->strings + 0x2b8);
    load_word(c, 2, R(3)); /* GPUSTAT is read twice by the original. */
    load_gpu_global(c, L, 5, L->data + 0x10c);
    load_gpu_global(c, L, 2, L->data + 0xe0);
    load_gpu_global(c, L, 8, L->data + 0x110);
    load_word(c, 2, R(2)); /* DMA2 address is the fifth printf argument */
    set_reg(c, 5, R(5) - R(8));
    MEM_W32(R(29) + 16, R(2));
    NC_POLL(c, 1);
    load_gpu_global(c, L, 2, L->data + 0xe8);
    load_word(c, 6, R(3));
    load_word(c, 7, R(2));
    R(31) = L->code + 0x36bc;
    R(5) &= 63;
    NC_POLL(c, 2);
    nc_call(c, L->printf);

    set_reg(c, 2, gpu_hi(L->data + 0xfc));
    set_reg(c, 2, L->data + 0xfc);
    load_word(c, 5, R(2));
    load_gpu_global(c, L, 6, L->data + 0x100);
    load_gpu_global(c, L, 7, L->data + 0x104);
    set_reg(c, 4, gpu_hi(L->strings + 0x2ec));
    set_reg(c, 4, L->strings + 0x2ec);
    gpu_sync_call(c, L->printf, L->code + 0x36e8);
}

static void reset_gpu_command_buffer(CPUState *c, const NcGpuLayout *L) {
    load_gpu_global(c, L, 3, L->data + 0xe8);
    set_reg(c, 2, 0x401);
    MEM_W32(R(3), R(2)); /* stop DMA2 */
    NC_POLL(c, 1);
    load_gpu_global(c, L, 3, L->data + 0xf8);
    NC_POLL(c, 1);
    load_word(c, 2, R(3));
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) | 0x800);
    MEM_W32(R(3), R(2)); /* enable DMA2 in DPCR */
    NC_POLL(c, 1);
    /* Acknowledge the GPU interrupt, then reset its command buffer. */
    for (unsigned command = 2; command > 0; command--) {
        load_gpu_global(c, L, 3, L->data + 0xdc);
        set_reg(c, 2, command << 24);
        MEM_W32(R(3), R(2));
        NC_POLL(c, 1);
    }
}

static void recover_gpu_queue(CPUState *c, const NcGpuLayout *L) {
    R(31) = L->code + 0x36f0;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, L->set_intr_mask); /* SetIntrMask(0) */
    store_gpu_global(c, L, L->data + 0x110, 0);
    load_gpu_global(c, L, 3, L->data + 0x110);
    store_gpu_global(c, L, L->data + 0x11c, 2);
    store_gpu_global(c, L, L->data + 0x10c, 3);

    reset_gpu_command_buffer(c, L);
    load_gpu_global(c, L, 4, L->data + 0x11c);
    gpu_sync_call(c, L->set_intr_mask, L->code + 0x376c);
    R(2) = UINT32_MAX;
    NC_POLL(c, 2);
}

/* Mode zero also resets GP1 and clears both driver buffers. */
static void reset_gpu_buffers(CPUState *c, const NcGpuLayout *L) {
    load_gpu_global(c, L, 3, L->data + 0xe8);
    NC_POLL(c, 1);
    MEM_W32(R(3), R(2)); /* DMA2 CHCR = 0x401 */
    NC_POLL(c, 1);
    load_gpu_global(c, L, 3, L->data + 0xf8);
    set_reg(c, 4, gpu_hi(L->bss + 0x44));
    set_reg(c, 4, L->bss + 0x44);
    load_word(c, 2, R(3));
    set_reg(c, 5, 0);
    set_reg(c, 2, R(2) | 0x800);
    MEM_W32(R(3), R(2));
    NC_POLL(c, 1);
    load_gpu_global(c, L, 2, L->data + 0xdc);
    set_reg(c, 6, 0x100);
    MEM_W32(R(2), 0);
    NC_POLL(c, 1);
    gpu_sync_call(c, L->code + 0x3864, L->code + 0x3400);
    set_reg(c, 4, gpu_hi(L->queue));
    set_reg(c, 4, L->queue);
    set_reg(c, 5, 0);
    R(31) = L->code + 0x3414;
    R(6) = 0x1800;
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x3864);
    NC_POLL(c, 2);
}

static bool sdk_gpu_reset_gpu_driver(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 6)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(31));
    R(31) = L->code + 0x335c;
    R(4) = 0;
    c->budget -= 3;
    nc_call(c, L->set_intr_mask); /* suspend interrupts while resetting the queue */
    store_gpu_global(c, L, L->data + 0x110, 0);
    load_gpu_global(c, L, 3, L->data + 0x110);
    store_gpu_global(c, L, L->data + 0x11c, 2);
    set_reg(c, 2, 1);
    store_gpu_global(c, L, L->data + 0x10c, 3);
    set_reg(c, 3, R(16) & 7);
    bool command_reset = R(3) == R(2);
    R(2) = (s32)R(3) < 2;
    NC_POLL(c, 2);
    if (!command_reset) {
        bool low_mode = R(2) != 0;
        NC_POLL(c, 2);
        if (low_mode) {
            bool full_reset = R(3) == 0;
            R(2) = 0x401;
            NC_POLL(c, 2);
            if (full_reset)
                reset_gpu_buffers(c, L);
            else
                NC_POLL(c, 2);
        } else {
            set_reg(c, 2, 3);
            command_reset = R(3) == R(2);
            NC_POLL(c, 2);
            if (!command_reset)
                NC_POLL(c, 2);
        }
    }
    if (command_reset)
        reset_gpu_command_buffer(c, L);

    load_gpu_global(c, L, 4, L->data + 0x11c);
    gpu_sync_call(c, L->set_intr_mask, L->code + 0x3478);
    set_reg(c, 2, R(16) & 7);
    bool query_version = R(2) == 0;
    R(2) = 0;
    NC_POLL(c, 2);
    if (query_version) {
        R(31) = L->code + 0x348c;
        R(4) = R(16);
        NC_POLL(c, 2);
        nc_call(c, L->code + 0x3788);
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_check_queue_alarm(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 4)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(31));
    R(31) = L->code + 0x362c;
    R(4) = UINT32_MAX;
    c->budget -= 3;
    nc_call(c, L->vsync); /* VSync(-1) */

    load_gpu_global(c, L, 3, L->data + 0x120);
    NC_POLL(c, 1);
    set_reg(c, 3, (s32)R(3) < (s32)R(2));
    bool expired = R(3) != 0;
    NC_POLL(c, 2);
    if (!expired) {
        load_gpu_global(c, L, 3, L->data + 0x124);
        NC_POLL(c, 1);
        set_reg(c, 2, R(3) + 1);
        store_gpu_global(c, L, L->data + 0x124, 2);
        set_reg(c, 2, 0xf0000);
        set_reg(c, 2, (s32)R(2) < (s32)R(3));
        expired = R(2) != 0;
        NC_POLL(c, 2);
    }
    if (expired) {
        report_gpu_timeout(c, L);
        recover_gpu_queue(c, L);
    } else {
        set_reg(c, 2, 0);
    }
    load_word(c, 31, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

static void load_gpu_queue_field(CPUState *c, const NcGpuLayout *L, unsigned dest,
                                 unsigned offset_reg, u32 field) {
    set_reg(c, 1, gpu_hi(field));
    set_reg(c, 1, R(1) + R(offset_reg));
    load_word(c, dest, R(1) + gpu_lo(field));
}

/* Multiply the ring index by the 96-byte queue entry size. */
static void gpu_queue_offset(CPUState *c, unsigned dest, unsigned index) {
    set_reg(c, dest, R(index) << 1);
    set_reg(c, dest, R(dest) + R(index));
    set_reg(c, dest, R(dest) << 5);
}

static void execute_gpu_queue_entry(CPUState *c, const NcGpuLayout *L) {
    load_gpu_global(c, L, 2, L->data + 0x110);
    load_gpu_global(c, L, 3, L->data + 0x10c);
    set_reg(c, 2, R(2) + 1);
    set_reg(c, 2, R(2) & 63);
    bool last = R(2) == R(3);
    NC_POLL(c, 2);
    if (last) {
        set_reg(c, 2, gpu_hi(L->data + 0x14));
        set_reg(c, 2, L->data + 0x14);
        load_word(c, 2, R(2));
        NC_POLL(c, 1);
        bool detach = !R(2);
        R(4) = 2;
        NC_POLL(c, 2);
        if (detach) {
            R(31) = L->code + 0x3100;
            R(5) = 0;
            NC_POLL(c, 2);
            nc_call(c, L->dma_callback); /* DMACallback(2, NULL) */
        }
    }

    load_gpu_global(c, L, 4, L->data + 0xdc);
    NC_POLL(c, 1);
    load_word(c, 2, R(4));
    set_reg(c, 3, 1u << 26);
    set_reg(c, 2, R(2) & R(3));
    bool ready = R(2) != 0;
    R(3) = R(4);
    NC_POLL(c, 2);
    if (!ready)
        set_reg(c, 4, 1u << 26);
    while (!ready) {
        load_word(c, 2, R(3));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & R(4));
        ready = R(2) != 0;
        NC_POLL(c, 2);
    }

    /* Read the command and its two arguments in the SDK's order. */
    load_gpu_global(c, L, 5, L->data + 0x110);
    load_gpu_global(c, L, 3, L->data + 0x110);
    NC_POLL(c, 1);
    gpu_queue_offset(c, 2, 3);
    set_reg(c, 3, R(5) << 1);
    set_reg(c, 3, R(3) + R(5));
    load_gpu_queue_field(c, L, 4, 2, L->queue + 4);
    load_gpu_global(c, L, 5, L->data + 0x110);
    set_reg(c, 3, R(3) << 5);
    gpu_queue_offset(c, 2, 5);
    load_gpu_queue_field(c, L, 5, 2, L->queue + 8);
    load_gpu_queue_field(c, L, 2, 3, L->queue);
    NC_POLL(c, 1);
    gpu_sync_call(c, R(2), L->code + 0x31a8);

    /* A callback can change the tail. Reload it before copying each word
     * to the driver's last-command record, and again before advancing it. */
    for (unsigned offset = 0; offset < 12; offset += 4) {
        load_gpu_global(c, L, 3, L->data + 0x110);
        NC_POLL(c, 1);
        gpu_queue_offset(c, 2, 3);
        load_gpu_queue_field(c, L, 2, 2, L->queue + offset);
        NC_POLL(c, 1);
        MEM_W32(R(16) + offset, R(2));
        NC_POLL(c, 1);
    }
    load_gpu_global(c, L, 2, L->data + 0x110);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) + 1);
    set_reg(c, 2, R(2) & 63);
    store_gpu_global(c, L, L->data + 0x110, 2);
}

static bool gpu_queue_empty(CPUState *c, const NcGpuLayout *L) {
    load_gpu_global(c, L, 3, L->data + 0x10c);
    load_gpu_global(c, L, 2, L->data + 0x110);
    NC_POLL(c, 1);
    bool empty = R(3) == R(2);
    NC_POLL(c, 2);
    return empty;
}

static bool queue_dma_idle(CPUState *c, const NcGpuLayout *L) {
    read_gpu_sync_bit(c, L, L->data + 0xe8, 1u << 24);
    bool idle = !R(2);
    NC_POLL(c, 2);
    return idle;
}

static void notify_gpu_queue_completion(CPUState *c, const NcGpuLayout *L) {
    if (!gpu_queue_empty(c, L) || !queue_dma_idle(c, L))
        return;
    set_reg(c, 3, gpu_hi(L->data + 0x10));
    set_reg(c, 3, L->data + 0x10);
    load_word(c, 2, R(3));
    NC_POLL(c, 1);
    bool pending = R(2) != 0;
    NC_POLL(c, 2);
    if (!pending)
        return;
    load_gpu_global(c, L, 2, L->data + 0x14);
    NC_POLL(c, 1);
    bool registered = R(2) != 0;
    NC_POLL(c, 2);
    if (registered) {
        MEM_W32(R(3), 0);
        NC_POLL(c, 1);
        load_gpu_global(c, L, 2, L->data + 0x14);
        NC_POLL(c, 1);
        gpu_sync_call(c, R(2), L->code + 0x3314);
    }
}

/* _exeque drains queued GPU commands until DMA starts or the ring is empty. */
static bool sdk_gpu_execute_gpu_queue(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 1)
        return false;
    load_gpu_global(c, L, 2, L->data + 0xe8);
    set_reg(c, 29, R(29) - 24);
    MEM_W32(R(29) + 20, R(31));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 16, R(16));
    NC_POLL(c, 1);
    load_word(c, 2, R(2));
    set_reg(c, 16, 1u << 24);
    set_reg(c, 2, R(2) & R(16));
    bool idle = !R(2);
    R(2) = 1;
    NC_POLL(c, 2);
    if (idle) {
        R(31) = L->code + 0x3078;
        R(4) = 0;
        NC_POLL(c, 2);
        nc_call(c, L->set_intr_mask); /* SetIntrMask */
        load_gpu_global(c, L, 4, L->data + 0x10c);
        load_gpu_global(c, L, 3, L->data + 0x110);
        store_gpu_global(c, L, L->data + 0x118, 2);
        bool queued = R(4) != R(3);
        NC_POLL(c, 2);
        if (queued) {
            load_gpu_global(c, L, 2, L->data + 0xe8);
            NC_POLL(c, 1);
            load_word(c, 2, R(2));
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) & R(16));
            idle = !R(2);
            NC_POLL(c, 2);
            if (idle) {
                set_reg(c, 16, gpu_hi(L->data + 0xfc));
                set_reg(c, 16, L->data + 0xfc);
                do {
                    execute_gpu_queue_entry(c, L);
                } while (!gpu_queue_empty(c, L) && queue_dma_idle(c, L));
            }
        }
        load_gpu_global(c, L, 4, L->data + 0x118);
        gpu_sync_call(c, L->set_intr_mask, L->code + 0x3294);
        notify_gpu_queue_completion(c, L);
        load_gpu_global(c, L, 2, L->data + 0x10c);
        load_gpu_global(c, L, 3, L->data + 0x110);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) - R(3));
        set_reg(c, 2, R(2) & 63);
    }
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

static void store_queue_field(CPUState *c, const NcGpuLayout *L, unsigned offset_reg, u32 field,
                              unsigned value_reg) {
    set_reg(c, 1, gpu_hi(field));
    set_reg(c, 1, R(1) + R(offset_reg));
    MEM_W32(R(1) + gpu_lo(field), R(value_reg));
    NC_POLL(c, 1);
}

static void store_queue_head_field(CPUState *c, const NcGpuLayout *L, u32 field,
                                   unsigned value_reg) {
    load_gpu_global(c, L, 3, L->data + 0x10c);
    NC_POLL(c, 1);
    gpu_queue_offset(c, 2, 3);
    store_queue_field(c, L, 2, field, value_reg);
}

/* Copy complete words to the inline payload. Nonzero sizes always select
 * inline storage, even when the signed byte count yields no whole words. */
static void copy_gpu_queue_payload(CPUState *c, const NcGpuLayout *L) {
    set_reg(c, 8, gpu_hi(L->queue + 0xc));
    set_reg(c, 8, L->queue + 0xc);
    set_reg(c, 7, R(16));
    set_reg(c, 2, R(17));
    for (;;) {
        bool negative = (s32)R(2) < 0;
        NC_POLL(c, 2);
        if (negative)
            set_reg(c, 2, R(2) + 3);
        set_reg(c, 2, (u32)((s32)R(2) >> 2));
        set_reg(c, 2, (s32)R(6) < (s32)R(2));
        bool more = R(2) != 0;
        R(4) = R(6) << 2;
        NC_POLL(c, 2);
        if (!more)
            break;
        load_word(c, 5, R(7));
        set_reg(c, 7, R(7) + 4);
        load_gpu_global(c, L, 3, L->data + 0x10c);
        set_reg(c, 6, R(6) + 1);
        gpu_queue_offset(c, 2, 3);
        set_reg(c, 2, R(2) + R(8));
        set_reg(c, 4, R(4) + R(2));
        MEM_W32(R(4), R(5));
        NC_POLL(c, 1);
        R(2) = R(17);
        NC_POLL(c, 2);
    }
    load_gpu_global(c, L, 2, L->data + 0x10c);
    load_gpu_global(c, L, 3, L->data + 0x10c);
    gpu_queue_offset(c, 4, 2);
    gpu_queue_offset(c, 2, 3);
    set_reg(c, 3, gpu_hi(L->queue + 0xc));
    set_reg(c, 3, L->queue + 0xc);
    set_reg(c, 2, R(2) + R(3));
    store_queue_field(c, L, 4, L->queue + 4, 2);
    NC_POLL(c, 2);
}

static void submit_queued_gpu_command(CPUState *c, const NcGpuLayout *L) {
    set_reg(c, 5, gpu_hi(L->code + 0x3048));
    set_reg(c, 5, L->code + 0x3048);
    R(31) = L->code + 0x2ec0;
    R(4) = 2;
    NC_POLL(c, 2);
    nc_call(c, L->dma_callback); /* DMACallback(2, _exeque) */
    bool copy = R(17) != 0;
    R(6) = 0;
    NC_POLL(c, 2);
    if (copy)
        copy_gpu_queue_payload(c, L);
    else
        store_queue_head_field(c, L, L->queue + 4, 16);
    store_queue_head_field(c, L, L->queue + 8, 18);
    store_queue_head_field(c, L, L->queue, 19);
    load_gpu_global(c, L, 2, L->data + 0x10c);
    load_gpu_global(c, L, 4, L->data + 0x114);
    set_reg(c, 2, R(2) + 1);
    set_reg(c, 2, R(2) & 63);
    store_gpu_global(c, L, L->data + 0x10c, 2);
    gpu_sync_call(c, L->set_intr_mask, L->code + 0x3004); /* restore interrupt mask */
    gpu_sync_call(c, L->code + 0x3048, L->code + 0x300c);
    load_gpu_global(c, L, 2, L->data + 0x10c);
    load_gpu_global(c, L, 3, L->data + 0x110);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) - R(3));
    set_reg(c, 2, R(2) & 63);
}

static void submit_immediate_gpu_command(CPUState *c, const NcGpuLayout *L) {
    load_gpu_global(c, L, 3, L->data + 0xdc);
    bool ready;
    do {
        NC_POLL(c, 1);
        load_word(c, 2, R(3));
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & R(4));
        ready = R(2) != 0;
        NC_POLL(c, 2);
    } while (!ready);
    set_reg(c, 4, R(16));
    u32 command = R(19);
    R(31) = L->code + 0x2e7c;
    R(5) = R(18);
    NC_POLL(c, 2);
    nc_call(c, command);
    load_gpu_global(c, L, 4, L->data + 0x114);
    set_reg(c, 2, gpu_hi(L->data + 0xfc));
    set_reg(c, 2, L->data + 0xfc);
    MEM_W32(R(2), R(19));
    NC_POLL(c, 1);
    store_gpu_global(c, L, L->data + 0x100, 16);
    store_gpu_global(c, L, L->data + 0x104, 18);
    gpu_sync_call(c, L->set_intr_mask, L->code + 0x2ea8);
    R(2) = 0;
    NC_POLL(c, 2);
}

static bool must_queue_gpu_command(CPUState *c, const NcGpuLayout *L) {
    R(31) = L->code + 0x2dd8;
    R(4) = 0;
    NC_POLL(c, 2);
    nc_call(c, L->set_intr_mask);
    set_reg(c, 3, gpu_hi(L->data + 0x10));
    set_reg(c, 3, L->data + 0x10);
    set_reg(c, 4, 1);
    MEM_W32(R(3), R(4));
    NC_POLL(c, 1);
    set_reg(c, 3, gpu_hi(L->data + 0x9));
    R(3) = MEM_R8(L->data + 0x9);
    NC_POLL(c, NC_LOAD_CYCLES);
    store_gpu_global(c, L, L->data + 0x114, 2);
    bool enabled = R(3) != 0;
    R(4) = 1u << 26;
    NC_POLL(c, 2);
    if (!enabled)
        return false;
    if (!gpu_queue_empty(c, L) || !queue_dma_idle(c, L))
        return true;
    load_gpu_global(c, L, 2, L->data + 0x14);
    NC_POLL(c, 1);
    bool callback = R(2) != 0;
    NC_POLL(c, 2);
    return callback;
}

static bool sdk_gpu_submit_gpu_command(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 11)
        return false;
    R(29) -= 40;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(4); /* command */
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(5); /* data */
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(6); /* byte count to copy */
    MEM_W32(R(29) + 24, R(18));
    MEM_W32(R(29) + 32, R(31));
    R(31) = L->code + 0x2d90;
    R(18) = R(7); /* second command argument */
    c->budget -= 11;
    nc_call(c, L->code + 0x35e8); /* set_alarm */
    NC_POLL(c, 2);
    for (;;) {
        load_gpu_global(c, L, 2, L->data + 0x10c);
        load_gpu_global(c, L, 3, L->data + 0x110);
        set_reg(c, 2, R(2) + 1);
        set_reg(c, 2, R(2) & 63);
        bool full = R(2) == R(3);
        NC_POLL(c, 2);
        if (!full) {
            if (must_queue_gpu_command(c, L))
                submit_queued_gpu_command(c, L);
            else
                submit_immediate_gpu_command(c, L);
            break;
        }
        if (gpu_sync_timed_out(c, L, L->code + 0x2da0))
            break;
        gpu_sync_call(c, L->code + 0x3048, L->code + 0x2db0);
    }
    load_word(c, 31, R(29) + 32);
    for (unsigned reg = 19; reg >= 16; reg--)
        load_word(c, reg, R(29) + 16 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_draw_offset_packet(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 43)
        return false;
    R(29) -= 24;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = 2;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget--;
    /* Write the tag first: its length byte may overlap the input coordinates. */
    MEM_W8(R(16) + 3, 2);
    c->budget--;
    R(4) = (u32)(s32)(s16)MEM_R16(R(5));
    c->budget -= NC_LOAD_CYCLES;
    R(5) = (u32)(s32)(s16)MEM_R16(R(5) + 2);
    R(31) = L->code + 0x1d1c;
    c->budget -= NC_LOAD_CYCLES + 2;
    nc_call(c, L->code + 0x2250);
    MEM_W32(R(16) + 4, R(2));
    c->budget--;
    MEM_W32(R(16) + 8, 0);
    c->budget--;
    R(31) = MEM_R32(R(29) + 20);
    c->budget -= NC_LOAD_CYCLES;
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 24;
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

static bool sdk_gpu_texture_window_word(CPUState *c) {
    const u32 rect = R(4);
    if (c->budget <= (rect ? 39 : 7))
        return false;

    R(29) -= 16;
    c->budget -= 2;
    if (rect) {
        /* The four five-bit fields are also spilled into the guest stack.
         * Read each rectangle member in order in case it overlaps a spill. */
        u32 x = MEM_R8(rect) >> 3;
        c->budget -= NC_LOAD_CYCLES + 2;
        MEM_W32(R(29), x);
        c->budget--;
        R(6) = ((0u - MEM_R16(rect + 4)) & 0xff) >> 3;
        c->budget -= NC_LOAD_CYCLES + 4;
        MEM_W32(R(29) + 8, R(6));
        c->budget--;
        u32 y = MEM_R8(rect + 2) >> 3;
        c->budget -= NC_LOAD_CYCLES + 2;
        MEM_W32(R(29) + 4, y);
        c->budget -= 2;
        R(3) = ((0u - MEM_R16(rect + 6)) & 0xff) >> 3;
        R(5) = 0xe2000000u | (x << 10);
        R(4) = R(3) << 5;
        R(2) = R(5) | (y << 15) | R(4) | R(6);
        c->budget -= NC_LOAD_CYCLES + 9;
        MEM_W32(R(29) + 12, R(3));
        c->budget--;
    } else {
        R(2) = 0;
        c->budget -= 2;
    }
    R(29) += 16;
    c->budget -= 3;
    return true;
}

static void load_env_byte(CPUState *c, unsigned reg, unsigned offset) {
    R(reg) = MEM_R8(R(16) + offset);
    NC_POLL(c, NC_LOAD_CYCLES);
}

static void store_draw_word(CPUState *c, u32 address, u32 value) {
    MEM_W32(address, value);
    NC_POLL(c, 1);
}

/* The optional clear uses a stack copy of the environment's clipping rectangle. */
static void prepare_env_clear(CPUState *c, const NcGpuLayout *L) {
    for (unsigned offset = 0; offset <= 4; offset += 2) {
        load_image_halfword(c, 2, R(16) + offset, false);
        NC_POLL(c, 1);
        MEM_W16(R(29) + 16 + offset, (u16)R(2));
        NC_POLL(c, 1);
    }
    load_image_halfword(c, 3, R(16) + 6, false);
    set_reg(c, 2, R(2) << 16);
    MEM_W16(R(29) + 22, (u16)R(3));
    NC_POLL(c, 1);
    set_reg(c, 3, (u32)((s32)R(2) >> 16));
    bool negative = (s32)R(3) < 0;
    R(2) = 0;
    NC_POLL(c, 2);
    if (!negative) {
        clear_rect_limit(c, L, L->data + 0xc);
        bool clip = R(2) != 0;
        R(2) = R(3);
        NC_POLL(c, 2);
        if (clip) {
            set_reg(c, 3, R(4));
            set_reg(c, 2, R(3));
        }
    }
    load_image_halfword(c, 3, R(29) + 22, true);
    NC_POLL(c, 1);
    negative = (s32)R(3) < 0;
    MEM_W16(R(29) + 20, (u16)R(2));
    NC_POLL(c, 2);
    if (negative) {
        set_reg(c, 2, 0);
    } else {
        clear_rect_limit(c, L, L->data + 0xe);
        bool clip = R(2) != 0;
        R(2) = R(3);
        NC_POLL(c, 2);
        if (clip) {
            set_reg(c, 3, R(4));
            R(2) = R(3);
            NC_POLL(c, 2);
        }
    }
    load_image_halfword(c, 3, R(29) + 16, false);
    MEM_W16(R(29) + 22, (u16)R(2));
    NC_POLL(c, 1);
}

static void pack_env_clear_color(CPUState *c) {
    load_env_byte(c, 2, 27);
    load_env_byte(c, 3, 26);
    set_reg(c, 2, R(2) << 16);
    set_reg(c, 3, R(3) << 8);
    set_reg(c, 3, R(3) | R(4));
    load_env_byte(c, 4, 25);
    set_reg(c, 2, R(2) | R(3));
    set_reg(c, 2, R(2) | R(4));
}

static void append_env_clear(CPUState *c, const NcGpuLayout *L) {
    prepare_env_clear(c, L);
    set_reg(c, 2, R(3) & 63);
    bool aligned = !R(2);
    R(6) = R(8) << 2;
    NC_POLL(c, 2);
    if (aligned) {
        load_image_halfword(c, 2, R(29) + 20, false);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & 63);
        aligned = !R(2);
        R(5) = R(8) << 2;
        NC_POLL(c, 2);
    }
    if (aligned) {
        /* A VRAM fill uses absolute coordinates. */
        set_reg(c, 8, R(8) + 1);
        set_reg(c, 6, R(8) << 2);
        set_reg(c, 8, R(8) + 1);
        set_reg(c, 7, R(8) << 2);
        set_reg(c, 8, R(8) + 1);
        set_reg(c, 5, R(5) + R(17));
        set_reg(c, 4, 0x02000000);
        pack_env_clear_color(c);
        store_draw_word(c, R(5), R(2));
        load_word(c, 2, R(29) + 16);
        set_reg(c, 6, R(6) + R(17));
        store_draw_word(c, R(6), R(2));
        load_word(c, 2, R(29) + 20);
        set_reg(c, 7, R(7) + R(17));
        store_draw_word(c, R(7), R(2));
        return;
    }
    /* A solid rectangle uses coordinates relative to the drawing offset. */
    set_reg(c, 8, R(8) + 1);
    set_reg(c, 5, R(8) << 2);
    set_reg(c, 8, R(8) + 1);
    load_image_halfword(c, 2, R(16) + 8, false);
    set_reg(c, 6, R(6) + R(17));
    set_reg(c, 2, R(3) - R(2));
    MEM_W16(R(29) + 16, (u16)R(2));
    NC_POLL(c, 1);
    load_image_halfword(c, 2, R(29) + 18, false);
    load_image_halfword(c, 3, R(16) + 10, false);
    set_reg(c, 4, 0x60000000);
    set_reg(c, 2, R(2) - R(3));
    MEM_W16(R(29) + 18, (u16)R(2));
    NC_POLL(c, 1);
    pack_env_clear_color(c);
    store_draw_word(c, R(6), R(2));
    load_word(c, 2, R(29) + 16);
    set_reg(c, 5, R(5) + R(17));
    store_draw_word(c, R(5), R(2));
    set_reg(c, 2, R(8) << 2);
    load_word(c, 3, R(29) + 20);
    set_reg(c, 2, R(2) + R(17));
    store_draw_word(c, R(2), R(3));
    load_image_halfword(c, 2, R(29) + 16, false);
    load_image_halfword(c, 3, R(16) + 8, false);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) + R(3));
    MEM_W16(R(29) + 16, (u16)R(2));
    NC_POLL(c, 1);
    load_image_halfword(c, 2, R(29) + 18, false);
    load_image_halfword(c, 3, R(16) + 10, false);
    set_reg(c, 8, R(8) + 1);
    set_reg(c, 2, R(2) + R(3));
    MEM_W16(R(29) + 18, (u16)R(2));
    NC_POLL(c, 2);
}

/* SetDrawEnv builds the DR_ENV packet from the caller's DRAWENV. */
static bool sdk_gpu_set_draw_env(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 6)
        return false;
    R(29) -= 40;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(5);
    MEM_W32(R(29) + 28, R(17));
    R(17) = R(4);
    MEM_W32(R(29) + 32, R(31));
    c->budget -= 6;
    load_image_halfword(c, 4, R(16), true);
    load_image_halfword(c, 5, R(16) + 2, true);
    gpu_sync_call(c, L->code + 0x20b8, L->code + 0x1de0); /* clip start */
    store_draw_word(c, R(17) + 4, R(2));
    load_image_halfword(c, 4, R(16) + 4, false);
    load_image_halfword(c, 2, R(16), false);
    load_image_halfword(c, 5, R(16) + 2, false);
    set_reg(c, 4, R(4) + R(2));
    set_reg(c, 4, R(4) - 1);
    set_reg(c, 4, R(4) << 16);
    load_image_halfword(c, 2, R(16) + 6, false);
    set_reg(c, 4, (u32)((s32)R(4) >> 16));
    set_reg(c, 5, R(5) + R(2));
    set_reg(c, 5, R(5) - 1);
    set_reg(c, 5, R(5) << 16);
    R(31) = L->code + 0x1e18;
    R(5) = (u32)((s32)R(5) >> 16);
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x2184); /* inclusive clip end */
    store_draw_word(c, R(17) + 8, R(2));
    load_image_halfword(c, 4, R(16) + 8, true);
    load_image_halfword(c, 5, R(16) + 10, true);
    gpu_sync_call(c, L->code + 0x2250, L->code + 0x1e2c); /* drawing offset */
    store_draw_word(c, R(17) + 12, R(2));
    load_env_byte(c, 4, 23);
    load_env_byte(c, 5, 22);
    load_image_halfword(c, 6, R(16) + 20, false);
    gpu_sync_call(c, L->code + 0x205c, L->code + 0x1e44); /* drawing mode */
    set_reg(c, 4, R(16) + 12);
    R(31) = L->code + 0x1e50;
    MEM_W32(R(17) + 16, R(2));
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x2298); /* texture window */
    store_draw_word(c, R(17) + 20, R(2));
    set_reg(c, 2, 0xe6000000);
    store_draw_word(c, R(17) + 24, R(2));
    load_env_byte(c, 2, 24);
    NC_POLL(c, 1);
    bool clear = R(2) != 0;
    R(8) = 7;
    NC_POLL(c, 2);
    if (clear)
        append_env_clear(c, L);
    set_reg(c, 2, R(8) - 1);
    MEM_W8(R(17) + 3, (u8)R(2));
    NC_POLL(c, 1);
    load_word(c, 31, R(29) + 32);
    load_word(c, 17, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_draw_mode_packet(CPUState *c, const NcGpuLayout *L) {
    /* Both command builders can run to completion within this budget. */
    if (c->budget <= 88)
        return false;

    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(4) = R(5);
    R(2) = 2;
    R(5) = R(6);
    c->budget -= 5;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    R(17) = MEM_R32(R(29) + 48); /* fifth argument: texture-window rectangle */
    R(6) = (u16)R(7);
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    R(31) = L->code + 0x1d90;
    MEM_W8(R(16) + 3, 2);
    c->budget -= 2;
    nc_call(c, L->code + 0x205c);
    MEM_W32(R(16) + 4, R(2));
    R(31) = L->code + 0x1d9c;
    R(4) = R(17);
    c->budget -= 3;
    NC_FN_ENTER(c, L->code + 0x2298);
    sdk_gpu_texture_window_word(c);
    MEM_W32(R(16) + 8, R(2));
    c->budget--;
    R(31) = MEM_R32(R(29) + 24);
    c->budget -= NC_LOAD_CYCLES;
    R(17) = MEM_R32(R(29) + 20);
    c->budget -= NC_LOAD_CYCLES;
    R(16) = MEM_R32(R(29) + 16);
    R(29) += 32;
    c->budget -= NC_LOAD_CYCLES + 3;
    return true;
}

static bool sdk_gpu_dump_texture_page(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 44)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(16));
    c->budget--;
    MEM_W32(R(29) + 28, R(31));
    R(16) = R(4);
    R(31) = L->code + 0x228;
    c->budget -= 3;
    nc_call(c, L->code + 0xdb4);
    bool alternate = R(2) == 1;
    c->budget -= 3;
    if (!alternate) {
        R(31) = L->code + 0x23c;
        c->budget -= 2;
        nc_call(c, L->code + 0xdb4);
        alternate = R(2) == 2;
        c->budget -= 3;
    }

    const u32 page = (u16)R(16);
    R(4) = L->strings;
    R(5) = (page >> (alternate ? 9 : 7)) & 3; /* depth */
    R(6) = (page >> (alternate ? 7 : 5)) & 3; /* blend mode */
    R(7) = (page << 6) & 0x7c0;               /* VRAM x */
    c->budget -= alternate ? 11 : 13;
    u32 callback = MEM_R32(L->data + 0x4);
    c->budget -= NC_LOAD_CYCLES + 1;
    u32 y;
    if (alternate) {
        R(3) = callback;
        y = R(2) = (page << 3) & 0x300;
        R(31) = L->code + 0x284;
    } else {
        R(8) = callback;
        R(2) = (page >> 2) & 0x200;
        y = R(3) = ((page << 4) & 0x100) | R(2);
        R(31) = L->code + 0x2d0;
    }
    MEM_W32(R(29) + 16, y);
    c->budget -= 2;
    nc_call(c, callback);
    if (alternate)
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Both environment diagnostics reload the callback before every report. */
static void report_fields(CPUState *c, const NcGpuLayout *L, u32 format, u32 resume,
                          bool rectangle) {
    unsigned reg = rectangle ? 3 : 2;
    set_reg(c, reg, gpu_hi(L->data + 4));
    load_word(c, reg, R(reg) + gpu_lo(L->data + 4));
    set_reg(c, 4, gpu_hi(format));
    set_reg(c, 4, format);
    u32 callback = R(reg);
    R(31) = resume;
    if (rectangle)
        MEM_W32(R(29) + 16, R(2));
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, callback);
}

static void report_rectangle(CPUState *c, const NcGpuLayout *L, unsigned offset, u32 format,
                             u32 resume) {
    for (unsigned j = 0; j < 4; j++) {
        R(j == 3 ? 2 : 5 + j) = (u32)(s32)(s16)MEM_R16(R(16) + offset + j * 2);
        NC_POLL(c, NC_LOAD_CYCLES);
    }
    report_fields(c, L, format, resume, true);
}

static void report_flag(CPUState *c, const NcGpuLayout *L, unsigned offset, u32 format,
                        u32 resume) {
    R(5) = MEM_R8(R(16) + offset);
    NC_POLL(c, NC_LOAD_CYCLES);
    report_fields(c, L, format, resume, false);
}

static void report_draw_page(CPUState *c, const NcGpuLayout *L) {
    R(31) = L->code + 0x8c0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, L->code + 0xdb4);
    set_reg(c, 3, 1);
    bool alternate = R(2) == 1;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    if (!alternate) {
        R(31) = L->code + 0x8d4;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, L->code + 0xdb4);
        set_reg(c, 3, 2);
        alternate = R(2) == 2;
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    }

    set_reg(c, 4, gpu_hi(L->strings));
    set_reg(c, 4, L->strings);
    R(2) = MEM_R16(R(16) + 20);
    NC_POLL(c, NC_LOAD_CYCLES);
    unsigned reg = alternate ? 3 : 8;
    set_reg(c, reg, gpu_hi(L->data + 4));
    load_word(c, reg, R(reg) + gpu_lo(L->data + 4));
    set_reg(c, 5, R(2) >> (alternate ? 9 : 7));
    set_reg(c, 5, R(5) & 3);
    set_reg(c, 6, R(2) >> (alternate ? 7 : 5));
    set_reg(c, 6, R(6) & 3);
    set_reg(c, 7, R(2) << 6);
    set_reg(c, 7, R(7) & 0x7c0);
    if (alternate) {
        set_reg(c, 2, R(2) << 3);
        set_reg(c, 2, R(2) & 0x300);
    } else {
        set_reg(c, 3, R(2) << 4);
        set_reg(c, 3, R(3) & 0x100);
        set_reg(c, 2, R(2) >> 2);
        set_reg(c, 2, R(2) & 0x200);
        set_reg(c, 3, R(3) + R(2));
    }
    u32 callback = R(reg);
    R(31) = alternate ? (L->code + 0x91c) : (L->code + 0x96c);
    MEM_W32(R(29) + 16, R(alternate ? 2 : 3)); /* VRAM y */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, callback);
    if (alternate)
        NC_POLL(c, 2 * NC_INSN_CYCLES);
}

static bool dump_environment(CPUState *c, const NcGpuLayout *L, bool draw) {
    if (c->budget <= 4)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;

    if (draw) {
        report_rectangle(c, L, 0, L->strings + 0x28, L->code + 0x838); /* clipping rectangle */
        for (unsigned j = 0; j < 2; j++) {
            R(5 + j) = (u32)(s32)(s16)MEM_R16(R(16) + 8 + j * 2);
            NC_POLL(c, NC_LOAD_CYCLES);
        }
        report_fields(c, L, L->strings + 0x40, L->code + 0x858, false); /* drawing offset */
        report_rectangle(c, L, 12, L->strings + 0x50, L->code + 0x880); /* texture window */
        report_flag(c, L, 22, L->strings + 0x68, L->code + 0x89c);      /* dithering */
        report_flag(c, L, 23, L->strings + 0x74, L->code + 0x8b8);      /* draw to display */
        report_draw_page(c, L);
    } else {
        report_rectangle(c, L, 0, L->strings + 0x80, L->code + 0x9b8);
        report_rectangle(c, L, 8, L->strings + 0x9c, L->code + 0x9e0);
        report_flag(c, L, 16, L->strings + 0xb8, L->code + 0x9fc); /* interlace */
        report_flag(c, L, 17, L->strings + 0xc4, L->code + 0xa18); /* RGB24 */
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static bool sdk_gpu_dump_draw(CPUState *c, const NcGpuLayout *L) {
    return dump_environment(c, L, true);
}

static bool sdk_gpu_dump_display(CPUState *c, const NcGpuLayout *L) {
    return dump_environment(c, L, false);
}

/* Sample each condition before polling for the branch and its delay slot. */
static bool rectangle_branch(CPUState *c, bool condition) {
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return condition;
}

/* Level one checks the rectangle against the signed VRAM limits in SDK order. */
static bool rectangle_out_of_bounds(CPUState *c, const NcGpuLayout *L) {
    R(5) = (s16)MEM_R16(R(16) + 4); /* width */
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 3, gpu_hi(L->data + 0xc));
    R(3) = (s16)MEM_R16(L->data + 0xc);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(3) < (s32)R(5));
    if (rectangle_branch(c, R(2)))
        return true;

    R(7) = (s16)MEM_R16(R(16)); /* x */
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(5) + R(7));
    set_reg(c, 2, (s32)R(3) < (s32)R(2));
    if (rectangle_branch(c, R(2)))
        return true;

    R(3) = (s16)MEM_R16(R(16) + 2); /* y */
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 4, gpu_hi(L->data + 0xe));
    R(4) = (s16)MEM_R16(L->data + 0xe);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(4) < (s32)R(3));
    if (rectangle_branch(c, R(2)))
        return true;

    R(6) = (s16)MEM_R16(R(16) + 6); /* height */
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(3) + R(6));
    set_reg(c, 2, (s32)R(4) < (s32)R(2));
    if (rectangle_branch(c, R(2)))
        return true;
    if (rectangle_branch(c, (s32)R(5) <= 0))
        return true;
    if (rectangle_branch(c, (s32)R(7) < 0))
        return true;
    if (rectangle_branch(c, (s32)R(3) < 0))
        return true;
    return rectangle_branch(c, (s32)R(6) <= 0);
}

static bool sdk_gpu_check_rect(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 8 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 32;
    R(8) = R(4); /* caller's operation name */
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(16));
    R(16) = R(5);
    c->budget -= 3;
    R(3) = MEM_R8(L->data + 0xa);
    R(2) = 1;
    c->budget -= NC_LOAD_CYCLES + 1;
    bool validate = R(3) == 1;
    MEM_W32(R(29) + 28, R(31));
    NC_POLL(c, 2);

    bool report;
    if (validate) {
        report = rectangle_out_of_bounds(c, L);
    } else {
        set_reg(c, 2, 2);
        report = R(3) == 2;
        NC_POLL(c, 2);
        if (!report)
            NC_POLL(c, 2);
    }
    if (report) {
        set_reg(c, 4, gpu_hi(L->strings + 0x1d4));
        set_reg(c, 4, validate ? (L->strings + 0x1d4) : (L->strings + 0x1f4));
        if (validate)
            NC_POLL(c, 2);
        set_reg(c, 2, gpu_hi(L->data + 0x4));
        load_word(c, 2, L->data + 0x4);
        NC_POLL(c, 1);
        R(31) = L->code + 0x1024;
        R(5) = R(8);
        NC_POLL(c, 2);
        nc_call(c, R(2));
        report_rectangle(c, L, 0, L->strings + 0x1e0, L->code + 0x104c);
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 16, R(29) + 24);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Both fills pack RGB from the low argument bytes. ClearImage2 also sets bit 31. */
static bool queue_image_fill(CPUState *c, const NcGpuLayout *L, bool high_bit) {
    const unsigned green = high_bit ? 16 : 17;
    const unsigned blue = high_bit ? 17 : 16;
    const unsigned driver = high_bit ? 3 : 2;
    if (c->budget <= 14)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(4);
    R(4) = L->strings + 0x1f8; /* "ClearImage" */
    c->budget -= 4;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(5);
    R(5) = R(19);
    c->budget -= 3;
    MEM_W32(R(29) + (high_bit ? 16 : 20), R(green));
    R(green) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + (high_bit ? 20 : 16), R(blue));
    c->budget--;
    MEM_W32(R(29) + 32, R(31));
    R(31) = high_bit ? (L->code + 0x112c) : (L->code + 0x1098);
    R(blue) = R(7);
    c->budget -= 3;
    nc_call(c, L->code + 0xf38); /* checkRECT */

    set_reg(c, 5, R(19));
    set_reg(c, blue, R(blue) & 0xff);
    set_reg(c, blue, R(blue) << 16);
    set_reg(c, green, R(green) & 0xff);
    set_reg(c, green, R(green) << 8);
    if (high_bit) {
        set_reg(c, 2, 0x80000000);
        set_reg(c, green, R(green) | R(2));
    }
    set_reg(c, blue, R(blue) | R(green));
    set_reg(c, 18, R(18) & 0xff);
    set_reg(c, driver, gpu_hi(L->data));
    load_word(c, driver, L->data);
    set_reg(c, 6, 8);
    load_word(c, 4, R(driver) + 0x0c);
    load_word(c, 2, R(driver) + 8);
    NC_POLL(c, 1);
    u32 enqueue = R(2);
    R(31) = high_bit ? (L->code + 0x1170) : (L->code + 0x10d4);
    R(7) = R(blue) | R(18);
    NC_POLL(c, 2);
    nc_call(c, enqueue);

    load_word(c, 31, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_clear_image(CPUState *c, const NcGpuLayout *L) {
    return queue_image_fill(c, L, false);
}

static bool sdk_gpu_clear_image2(CPUState *c, const NcGpuLayout *L) {
    return queue_image_fill(c, L, true);
}

/* Validate the VRAM rectangle, then queue its eight bytes and the pixel buffer. */
static bool queue_image_transfer(CPUState *c, const NcGpuLayout *L, bool store) {
    if (c->budget <= 10)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(5);
    R(4) = store ? (L->strings + 0x210) : (L->strings + 0x204); /* name passed to checkRECT */
    c->budget -= 4;
    MEM_W32(R(29) + 24, R(31));
    R(31) = store ? (L->code + 0x121c) : (L->code + 0x11b8);
    R(5) = R(16);
    c->budget -= 3;
    nc_call(c, L->code + 0xf38); /* checkRECT */

    set_reg(c, 5, R(16));
    set_reg(c, 2, gpu_hi(L->data));
    load_word(c, 2, L->data);
    set_reg(c, 6, 8);
    load_word(c, 4, R(2) + (store ? 0x1c : 0x20));
    load_word(c, 2, R(2) + 8);
    NC_POLL(c, 1);
    u32 enqueue = R(2);
    R(31) = store ? (L->code + 0x1240) : (L->code + 0x11dc);
    R(7) = R(17);
    NC_POLL(c, 2);
    nc_call(c, enqueue);
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_load_image(CPUState *c, const NcGpuLayout *L) {
    return queue_image_transfer(c, L, false);
}

static bool sdk_gpu_store_image(CPUState *c, const NcGpuLayout *L) {
    return queue_image_transfer(c, L, true);
}

/* Save the graphics call argument and test the SDK debug level. */
static void graphics_debug_prologue(CPUState *c, const NcGpuLayout *L) {
    c->budget--;
    R(2) = MEM_R8(L->data + 0xa);
    R(29) -= 24;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = R(2) < 2;
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(31));
    c->budget -= 2;
}

static void graphics_diagnostic(CPUState *c, const NcGpuLayout *L, u32 format, u32 return_pc,
                                unsigned argument_reg) {
    if (!R(2)) {
        set_reg(c, 4, gpu_hi(format));
        set_reg(c, 4, format);
        set_reg(c, 2, gpu_hi(L->data + 0x4));
        load_word(c, 2, L->data + 0x4);
        NC_POLL(c, 1);
        u32 diagnostic = R(2);
        R(31) = return_pc;
        R(5) = R(argument_reg);
        NC_POLL(c, 2);
        nc_call(c, diagnostic);
    }
}

static void build_draw_env_packet(CPUState *c, const NcGpuLayout *L, u32 resume) {
    set_reg(c, 16, R(17) + 28); /* embedded packet */
    set_reg(c, 4, R(16));
    R(31) = resume;
    R(5) = R(17);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, L->code + 0x1db8); /* SetDrawEnv */

    set_reg(c, 4, 0x00ff0000);
    set_reg(c, 4, 0x00ffffff);
    set_reg(c, 5, R(16));
    set_reg(c, 6, 64); /* packet bytes copied by the queue */
}

/* Merge the link address into the packet header, enqueue it and cache the DRAWENV. */
static void submit_draw_env(CPUState *c, const NcGpuLayout *L, unsigned debug_reg, u32 queue_resume,
                            u32 copy_resume) {
    set_reg(c, 3, gpu_hi(L->data));
    load_word(c, 3, L->data);
    set_reg(c, 2, R(2) | R(4));
    MEM_W32(R(17) + 28, R(2));
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 4, R(3) + 0x18);
    load_word(c, 2, R(3) + 8);
    NC_POLL(c, NC_INSN_CYCLES);
    u32 enqueue = R(2);
    R(31) = queue_resume;
    R(7) = 0;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, enqueue);

    set_reg(c, 4, R(debug_reg) + 14);
    set_reg(c, 5, R(17));
    R(31) = copy_resume;
    R(6) = 92; /* full DRAWENV, including the packet */
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, L->memcpy); /* memcpy */
}

static bool display_rect_changed(CPUState *c, const NcGpuLayout *L, unsigned offset) {
    for (unsigned i = offset; i < offset + 8; i += 2) {
        u32 cached = (L->data + 0x74) + i;
        set_reg(c, 2, gpu_hi(cached));
        if (i == 8)
            set_reg(c, 2, cached);
        load_image_halfword(c, 2, cached, false);
        load_image_halfword(c, 3, R(16) + i, true);
        set_reg(c, 2, R(2) << 16);
        set_reg(c, 2, (u32)((s32)R(2) >> 16));
        bool changed = R(2) != R(3);
        NC_POLL(c, 2);
        if (changed)
            return true;
    }
    return false;
}

static void set_display_origin(CPUState *c, const NcGpuLayout *L) {
    set_reg(c, 2, gpu_hi(L->data + 0x8));
    R(2) = MEM_R8(L->data + 0x8);
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) - 1);
    set_reg(c, 2, R(2) < 2);
    bool wide_fields = R(2) != 0;
    NC_POLL(c, 2);
    if (wide_fields) {
        R(31) = L->code + 0x1790;
        R(4) = R(16);
        NC_POLL(c, 2);
        nc_call(c, L->code + 0x231c); /* get_dx */
        load_image_halfword(c, 3, R(16) + 2, false);
        set_reg(c, 2, R(2) & 0xfff);
        set_reg(c, 3, R(3) & 0xfff);
        set_reg(c, 3, R(3) << 12);
        set_reg(c, 3, R(3) | R(2));
        R(2) = 0x05000000;
        NC_POLL(c, 2);
    } else {
        load_image_halfword(c, 2, R(16) + 2, false);
        load_image_halfword(c, 3, R(16), false);
        set_reg(c, 2, R(2) & 0x3ff);
        set_reg(c, 2, R(2) << 10);
        set_reg(c, 3, R(3) & 0x3ff);
        set_reg(c, 2, R(2) | R(3));
        set_reg(c, 3, 0x05000000);
    }
    set_reg(c, 4, R(2) | R(3));
    load_gpu_global(c, L, 2, L->data);
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 16);
    NC_POLL(c, 1);
    gpu_sync_call(c, R(2), L->code + 0x17e8);
}

/* Clamp the vertical start or end to the video standard's scanline limit. */
static void limit_display_scanline(CPUState *c, bool end) {
    unsigned src = end ? 18 : 17, dest = end ? 3 : 4;
    u32 ntsc_max = end ? 258 : 256, pal_max = end ? 312 : 310;
    load_env_byte(c, 2, 18);
    NC_POLL(c, 1);
    bool pal = R(2) != 0;
    R(2) = (s32)R(src) <= (s32)pal_max;
    NC_POLL(c, 2);
    if (!pal)
        set_reg(c, 2, (s32)R(src) <= (s32)ntsc_max);
    bool fits = R(2) != 0;
    NC_POLL(c, 2);
    if (fits) {
        R(dest) = R(src);
        NC_POLL(c, end && !pal ? 1 : 2);
    } else {
        load_env_byte(c, 2, 18);
        NC_POLL(c, 1);
        pal = R(2) != 0;
        R(dest) = ntsc_max;
        NC_POLL(c, 2);
        if (pal) {
            R(dest) = pal_max;
            NC_POLL(c, 2);
        }
    }
}

static void send_display_range(CPUState *c, const NcGpuLayout *L, bool vertical) {
    u32 mask = vertical ? 0x3ff : 0xfff;
    set_reg(c, 2, R(vertical ? 18 : 6) & mask);
    set_reg(c, 2, R(2) << (vertical ? 10 : 12));
    set_reg(c, 4, R(vertical ? 17 : 5) & mask);
    set_reg(c, 3, vertical ? 0x07000000 : 0x06000000);
    load_gpu_global(c, L, 5, L->data);
    set_reg(c, 4, R(4) | R(3));
    load_word(c, 3, R(5) + 16);
    NC_POLL(c, 1);
    u32 driver = R(3);
    R(31) = vertical ? (L->code + 0x1a20) : (L->code + 0x19f4);
    R(4) |= R(2);
    NC_POLL(c, 2);
    nc_call(c, driver);
}

static void set_display_ranges(CPUState *c, const NcGpuLayout *L) {
    gpu_sync_call(c, L->code + 0x124, L->code + 0x1864); /* GetVideoMode */
    load_image_halfword(c, 3, R(16) + 8, true);
    MEM_W8(R(16) + 18, (u8)R(2));
    NC_POLL(c, 1);
    set_reg(c, 2, R(3) << 2);
    set_reg(c, 2, R(2) + R(3));
    set_reg(c, 2, R(2) << 1);
    set_reg(c, 5, R(2) + 608); /* horizontal start in GPU clock units */
    load_env_byte(c, 2, 18);
    load_image_halfword(c, 4, R(16) + 10, true);
    bool pal = R(2) != 0;
    R(17) = R(4) + 19;
    NC_POLL(c, 2);
    if (!pal)
        set_reg(c, 17, R(4) + 16);
    load_image_halfword(c, 3, R(16) + 12, true);
    NC_POLL(c, 1);
    bool width = R(3) != 0;
    R(2) = R(3) << 2;
    NC_POLL(c, 2);
    if (width) {
        set_reg(c, 2, R(2) + R(3));
        set_reg(c, 2, R(2) << 1);
        R(6) = R(5) + R(2);
        NC_POLL(c, 2);
    } else {
        set_reg(c, 6, R(5) + 2560);
    }
    load_image_halfword(c, 2, R(16) + 14, true);
    NC_POLL(c, 1);
    bool height = R(2) != 0;
    R(18) = R(17) + R(2);
    NC_POLL(c, 2);
    if (!height)
        set_reg(c, 18, R(17) + 240);

    set_reg(c, 2, (s32)R(5) < 500);
    bool low = R(2) != 0;
    R(4) = 500;
    NC_POLL(c, 2);
    if (low) {
        set_reg(c, 5, R(4));
    } else {
        set_reg(c, 4, R(5));
        set_reg(c, 2, (s32)R(4) <= 3290);
        bool fits = R(2) != 0;
        R(5) = R(4);
        NC_POLL(c, 2);
        if (!fits) {
            set_reg(c, 4, 3290);
            set_reg(c, 5, R(4));
        }
    }
    set_reg(c, 3, R(5) + 80);
    set_reg(c, 2, (s32)R(6) < (s32)R(3));
    bool narrow = R(2) != 0;
    R(2) = (s32)R(17) < 16;
    NC_POLL(c, 2);
    if (!narrow) {
        set_reg(c, 3, R(6));
        set_reg(c, 2, (s32)R(3) <= 3290);
        bool fits = R(2) != 0;
        R(2) = (s32)R(17) < 16;
        NC_POLL(c, 2);
        if (!fits)
            set_reg(c, 3, 3290);
    }
    low = R(2) != 0;
    R(6) = R(3);
    NC_POLL(c, 2);
    if (low)
        set_reg(c, 4, 16);
    else
        limit_display_scanline(c, false);
    set_reg(c, 17, R(4));
    set_reg(c, 3, R(17) + 2);
    set_reg(c, 2, (s32)R(18) < (s32)R(3));
    bool short_range = R(2) != 0;
    NC_POLL(c, 2);
    if (!short_range)
        limit_display_scanline(c, true);
    set_reg(c, 18, R(3));
    send_display_range(c, L, false);
    send_display_range(c, L, true);
}

static void set_display_resolution(CPUState *c) {
    load_image_halfword(c, 3, R(16) + 4, true);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(3) <= 280);
    bool small = R(2) != 0;
    R(2) = (s32)R(3) <= 352;
    NC_POLL(c, 2);
    if (!small) {
        bool medium = R(2) != 0;
        R(2) = (s32)R(3) <= 400;
        NC_POLL(c, 2);
        if (medium) {
            R(19) |= 1;
            NC_POLL(c, 2);
        } else {
            bool special = R(2) != 0;
            R(2) = (s32)R(3) <= 560;
            NC_POLL(c, 2);
            if (special) {
                R(19) |= 64;
                NC_POLL(c, 2);
            } else {
                bool wide = R(2) != 0;
                NC_POLL(c, 2);
                R(19) |= wide ? 2 : 3;
                NC_POLL(c, wide ? 2 : 1);
            }
        }
    }
    load_env_byte(c, 2, 18);
    load_image_halfword(c, 3, R(16) + 6, true);
    bool pal = R(2) != 0;
    R(2) = (s32)R(3) <= 288;
    NC_POLL(c, 2);
    if (!pal)
        set_reg(c, 2, (s32)R(3) <= 256);
    bool tall = !R(2);
    NC_POLL(c, 2);
    if (tall)
        set_reg(c, 19, R(19) | 0x24);
}

static void set_display_mode(CPUState *c, const NcGpuLayout *L) {
    gpu_sync_call(c, L->code + 0x124, L->code + 0x1ab0);
    MEM_W8(R(16) + 18, (u8)R(2));
    NC_POLL(c, 1);
    load_env_byte(c, 3, 18);
    set_reg(c, 2, 1);
    bool pal = R(3) == R(2);
    NC_POLL(c, 2);
    if (pal)
        set_reg(c, 19, R(19) | 8);
    for (unsigned i = 0; i < 3; i++) {
        if (i < 2) {
            load_env_byte(c, 2, 17 - i); /* RGB24 and interlace */
        } else {
            set_reg(c, 2, gpu_hi(L->data + 0xb));
            set_reg(c, 2, L->data + 0xb);
            R(2) = MEM_R8(R(2)); /* reverse display */
            NC_POLL(c, NC_LOAD_CYCLES);
        }
        NC_POLL(c, 1);
        bool enabled = R(2) != 0;
        NC_POLL(c, 2);
        if (enabled)
            set_reg(c, 19, R(19) | (i == 2 ? 0x80 : 0x10u << i));
    }
    set_display_resolution(c);
    load_gpu_global(c, L, 2, L->data);
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 16);
    NC_POLL(c, 1);
    u32 driver = R(2);
    R(31) = L->code + 0x1b90;
    R(4) = R(19);
    NC_POLL(c, 2);
    nc_call(c, driver);
}

static bool sdk_gpu_put_display_env(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 9)
        return false;
    R(29) -= 40;
    MEM_W32(R(29) + 16, R(16));
    R(16) = R(4);
    R(2) = gpu_hi(L->data + 0xa);
    R(2) = L->data + 0xa;
    MEM_W32(R(29) + 32, R(31));
    MEM_W32(R(29) + 28, R(19));
    MEM_W32(R(29) + 24, R(18));
    MEM_W32(R(29) + 20, R(17));
    c->budget -= 9;
    R(2) = MEM_R8(R(2));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) < 2);
    bool debug = !R(2);
    R(19) = 0x08000000;
    NC_POLL(c, 2);
    if (debug) {
        set_reg(c, 4, gpu_hi(L->strings + 0x2a0));
        set_reg(c, 4, L->strings + 0x2a0);
        load_gpu_global(c, L, 2, L->data + 0x4);
        NC_POLL(c, 1);
        u32 printer = R(2);
        R(31) = L->code + 0x176c;
        R(5) = R(16);
        NC_POLL(c, 2);
        nc_call(c, printer);
    }
    set_display_origin(c, L);
    if (display_rect_changed(c, L, 8))
        set_display_ranges(c, L);
    load_gpu_global(c, L, 3, L->data + 0x84);
    load_word(c, 2, R(16) + 16);
    NC_POLL(c, 1);
    bool changed = R(3) != R(2);
    NC_POLL(c, 2);
    if (changed || display_rect_changed(c, L, 0))
        set_display_mode(c, L);
    set_reg(c, 4, gpu_hi(L->data + 0x74));
    set_reg(c, 4, L->data + 0x74);
    set_reg(c, 5, R(16));
    R(31) = L->code + 0x1ba4;
    R(6) = 20;
    NC_POLL(c, 2);
    nc_call(c, L->memcpy);
    set_reg(c, 2, R(16));
    load_word(c, 31, R(29) + 32);
    for (unsigned reg = 19; reg >= 16; reg--)
        load_word(c, reg, R(29) + 16 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

/* Build the embedded draw packet, submit it and save the active DRAWENV. */
static bool sdk_gpu_put_draw_env(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 11 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    R(18) = L->data + 0xa;
    c->budget -= 3;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(2) = MEM_R8(R(18)) < 2;
    R(17) = R(4); /* environment */
    c->budget -= NC_LOAD_CYCLES + 4;
    graphics_diagnostic(c, L, L->strings + 0x26c, L->code + 0x158c, 17);

    build_draw_env_packet(c, L, L->code + 0x159c);
    load_word(c, 2, R(17) + 28);
    submit_draw_env(c, L, 18, L->code + 0x15d4, L->code + 0x15e4);
    set_reg(c, 2, R(17));
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* Prepend the draw environment packet to the supplied ordering table. */
static bool sdk_gpu_draw_otag_env(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 13 * NC_INSN_CYCLES + NC_LOAD_CYCLES)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    R(18) = R(4); /* ordering table */
    c->budget -= 2;
    MEM_W32(R(29) + 28, R(19));
    R(19) = L->data + 0xa;
    c->budget -= 3;
    MEM_W32(R(29) + 32, R(31));
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    c->budget--;
    R(2) = MEM_R8(R(19)) < 2;
    R(17) = R(5); /* environment */
    c->budget -= NC_LOAD_CYCLES + 4;
    if (!R(2)) {
        set_reg(c, 4, gpu_hi(L->strings + 0x284));
        set_reg(c, 4, L->strings + 0x284);
        set_reg(c, 5, R(18));
        set_reg(c, 2, gpu_hi(L->data + 0x4));
        load_word(c, 2, L->data + 0x4);
        NC_POLL(c, NC_INSN_CYCLES);
        u32 diagnostic = R(2);
        R(31) = L->code + 0x165c;
        R(6) = R(17);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, diagnostic);
    }
    build_draw_env_packet(c, L, L->code + 0x166c);
    set_reg(c, 3, 0xff000000);
    load_word(c, 2, R(17) + 28);
    set_reg(c, 4, R(18) & R(4)); /* 24-bit ordering table address */
    set_reg(c, 2, R(2) & R(3));  /* retain the packet length */
    submit_draw_env(c, L, 19, L->code + 0x16b0, L->code + 0x16c0);
    load_word(c, 31, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

/* DrawOTag queues the active driver's DMA-chain method with the ordering table. */
static bool sdk_gpu_draw_otag(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 10)
        return false;
    graphics_debug_prologue(c, L);
    graphics_diagnostic(c, L, L->strings + 0x258, L->code + 0x1508, 16);
    set_reg(c, 5, R(16));
    set_reg(c, 2, gpu_hi(L->data));
    load_word(c, 2, L->data);
    set_reg(c, 6, 0);
    load_word(c, 4, R(2) + 0x18);
    load_word(c, 2, R(2) + 8);
    NC_POLL(c, 1);
    u32 enqueue = R(2);
    R(31) = L->code + 0x152c;
    R(7) = 0;
    NC_POLL(c, 2);
    nc_call(c, enqueue);

    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_draw_sync_callback(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 10)
        return false;
    graphics_debug_prologue(c, L);
    if (!R(2)) {
        set_reg(c, 2, gpu_hi(L->data + 4));
        load_word(c, 2, R(2) + gpu_lo(L->data + 4));
        set_reg(c, 4, gpu_hi(L->strings + 0x190));
        set_reg(c, 4, L->strings + 0x190);
        u32 diagnostic = R(2);
        R(31) = L->code + 0xe0c;
        R(5) = R(16);
        NC_POLL(c, 2 * NC_INSN_CYCLES);
        nc_call(c, diagnostic);
    }

    /* Read the previous handler after the diagnostic returns. */
    set_reg(c, 2, gpu_hi(L->data + 0x14));
    load_word(c, 2, R(2) + gpu_lo(L->data + 0x14));
    set_reg(c, 1, gpu_hi(L->data + 0x14));
    MEM_W32(R(1) + gpu_lo(L->data + 0x14), R(16));
    NC_POLL(c, NC_INSN_CYCLES);
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

static bool sdk_gpu_draw_sync(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 10)
        return false;
    graphics_debug_prologue(c, L);
    graphics_diagnostic(c, L, L->strings + 0x1c0, L->code + 0xf08, 16);

    /* The diagnostic can replace the active driver before this lookup. */
    set_reg(c, 2, gpu_hi(L->data));
    load_word(c, 2, L->data);
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 0x3c);
    NC_POLL(c, 1);
    u32 sync = R(2);
    R(31) = L->code + 0xf24;
    R(4) = R(16);
    NC_POLL(c, 2);
    nc_call(c, sync);
    load_word(c, 31, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 24);
    NC_POLL(c, 2);
    return true;
}

static void fill_graphics_state(CPUState *c, const NcGpuLayout *L, u32 length, u32 resume) {
    R(31) = resume;
    R(6) = length;
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x3864);
}

static void initialize_graphics_state(CPUState *c, const NcGpuLayout *L) {
    set_reg(c, 4, gpu_hi(L->strings + 0x104));
    set_reg(c, 4, L->strings + 0x104);
    set_reg(c, 5, gpu_hi(L->data - 0x40));
    set_reg(c, 5, L->data - 0x40);
    set_reg(c, 16, gpu_hi(L->data + 0x8));
    set_reg(c, 16, L->data + 0x8);
    R(31) = L->code + 0xa74;
    R(6) = R(16);
    NC_POLL(c, 2);
    nc_call(c, L->printf); /* printf */
    set_reg(c, 4, R(16));
    set_reg(c, 5, 0);
    fill_graphics_state(c, L, 0x80, L->code + 0xa84);
    gpu_sync_call(c, L->reset_callback, L->code + 0xa8c); /* ResetCallback */
    set_reg(c, 2, 0x00ff0000);
    load_gpu_global(c, L, 4, L->data);
    set_reg(c, 2, R(2) | 0xffff);
    R(31) = L->code + 0xaa4;
    R(4) &= R(2);
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x3890); /* GPU_cw */
    R(31) = L->code + 0xaac;
    R(4) = R(17);
    NC_POLL(c, 2);
    nc_call(c, L->code + 0x3344);

    /* The reset result selects the VRAM width and height tables by low byte. */
    set_reg(c, 4, R(16) + 16);
    MEM_W8(R(16), (u8)R(2));
    NC_POLL(c, 1);
    R(2) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 3, 1);
    set_reg(c, 1, gpu_hi(L->data + 0x9));
    MEM_W8(L->data + 0x9, (u8)R(3));
    NC_POLL(c, 1);
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 1, gpu_hi(L->data + 0x88));
    set_reg(c, 1, R(1) + R(2));
    R(3) = MEM_R16(R(1) + gpu_lo(L->data + 0x88));
    NC_POLL(c, NC_LOAD_CYCLES);
    R(2) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 5, UINT32_MAX);
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 1, gpu_hi(L->data + 0xc));
    MEM_W16(L->data + 0xc, (u16)R(3));
    NC_POLL(c, 1);
    set_reg(c, 1, gpu_hi(L->data + 0x9c));
    set_reg(c, 1, R(1) + R(2));
    R(2) = MEM_R16(R(1) + gpu_lo(L->data + 0x9c));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 1, gpu_hi(L->data + 0xe));
    MEM_W16(L->data + 0xe, (u16)R(2));
    NC_POLL(c, 1);
    fill_graphics_state(c, L, 0x5c, L->code + 0xb04);
    set_reg(c, 4, R(16) + 0x6c);
    set_reg(c, 5, UINT32_MAX);
    fill_graphics_state(c, L, 0x14, L->code + 0xb14);
    R(2) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 2);
}

static bool sdk_gpu_reset_graph(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 7)
        return false;
    R(29) -= 32;
    c->budget--;
    MEM_W32(R(29) + 20, R(17));
    R(17) = R(4);
    R(3) = R(17) & 7;
    c->budget -= 3;
    MEM_W32(R(29) + 24, R(31));
    c->budget--;
    bool initialize = R(3) == 0;
    MEM_W32(R(29) + 16, R(16));
    c->budget -= 2;
    if (!initialize) {
        set_reg(c, 2, 3);
        initialize = R(3) == R(2);
        NC_POLL(c, 2);
    }
    if (initialize) {
        initialize_graphics_state(c, L);
    } else {
        set_reg(c, 2, gpu_hi(L->data + 0xa));
        R(2) = MEM_R8(L->data + 0xa);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) < 2);
        NC_POLL(c, 2);
        graphics_diagnostic(c, L, L->strings + 0x124, L->code + 0xb54, 17);
        load_gpu_global(c, L, 2, L->data);
        NC_POLL(c, 1);
        load_word(c, 2, R(2) + 0x34);
        NC_POLL(c, 1);
        u32 reset = R(2);
        R(31) = L->code + 0xb70;
        R(4) = 1;
        NC_POLL(c, 2);
        nc_call(c, reset);
    }
    load_word(c, 31, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

/* Set the GPU display-mode reverse bit and return the previous flag byte. */
static bool sdk_gpu_set_graph_reverse(CPUState *c, const NcGpuLayout *L) {
    const int entry_cycles = 11 * NC_INSN_CYCLES + 2 * NC_LOAD_CYCLES;
    if (c->budget <= entry_cycles)
        return false;
    c->budget--;
    R(2) = MEM_R8(L->data + 0xa);
    R(29) -= 32;
    c->budget -= NC_LOAD_CYCLES + 1;
    MEM_W32(R(29) + 20, R(17));
    c->budget--;
    MEM_W32(R(29) + 16, R(16));
    R(16) = L->data + 0xb;
    c->budget -= 3;
    MEM_W32(R(29) + 28, R(31));
    c->budget--;
    MEM_W32(R(29) + 24, R(18));
    c->budget--;
    R(18) = MEM_R8(R(16));
    R(2) = R(2) < 2;
    R(17) = R(4);
    c->budget -= NC_LOAD_CYCLES + 3;
    graphics_diagnostic(c, L, L->strings + 0x138, L->code + 0xbd8, 17);

    /* Query GPU parameter 8, then combine it with the current reverse flag. */
    set_reg(c, 2, gpu_hi(L->data));
    load_word(c, 2, L->data);
    MEM_W8(R(16), (u8)R(17));
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 0x28);
    NC_POLL(c, 1);
    R(31) = L->code + 0xbf4;
    R(4) = 8;
    NC_POLL(c, 2);
    nc_call(c, R(2));
    R(3) = MEM_R8(R(16));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    bool reverse = R(3) != 0;
    R(4) = R(2);
    NC_POLL(c, 2);
    set_reg(c, 2, 0x08000000);
    if (reverse) {
        R(2) |= 0x80;
        NC_POLL(c, 2);
    }
    set_reg(c, 4, R(4) | R(2));
    set_reg(c, 2, gpu_hi(L->data));
    load_word(c, 2, L->data);
    NC_POLL(c, 1);
    load_word(c, 2, R(2) + 0x10);
    NC_POLL(c, 1);
    R(31) = L->code + 0xc34;
    NC_POLL(c, 2);
    nc_call(c, R(2));

    /* Graphics type 2 needs a second control command. */
    set_reg(c, 3, gpu_hi(L->data + 0x8));
    R(3) = MEM_R8(L->data + 0x8);
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 2, 2);
    bool extra_command = R(3) == R(2);
    R(2) = R(18);
    NC_POLL(c, 2);
    if (extra_command) {
        set_reg(c, 4, 0x20000000);
        set_reg(c, 2, gpu_hi(L->data + 0xb));
        R(2) = MEM_R8(L->data + 0xb);
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, 3, gpu_hi(L->data));
        load_word(c, 3, L->data);
        reverse = R(2) != 0;
        R(4) = 0x20000504;
        NC_POLL(c, 2);
        if (reverse) {
            set_reg(c, 4, 0x20000000);
            set_reg(c, 4, 0x20000501);
        }
        load_word(c, 2, R(3) + 0x10);
        NC_POLL(c, 1);
        R(31) = L->code + 0xc7c;
        NC_POLL(c, 2);
        nc_call(c, R(2));
        set_reg(c, 2, R(18));
    }
    load_word(c, 31, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 32);
    NC_POLL(c, 2);
    return true;
}

static bool sdk_gpu_texture_page(CPUState *c, const NcGpuLayout *L) {
    /* The longest path reads the graph type twice and uses the standard layout. */
    if (c->budget <= 62)
        return false;

    R(29) -= 0x28;
    c->budget--;
    MEM_W32(R(29) + 0x10, R(16));
    R(16) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 0x18, R(18));
    R(18) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + 0x14, R(17));
    c->budget--;
    MEM_W32(R(29) + 0x20, R(31));
    R(31) = L->code + 0x160;
    R(17) = R(7);
    c->budget -= 3;

    nc_call(c, L->code + 0xdb4);
    bool alternate_layout = R(2) == 1;
    c->budget -= 3;
    if (!alternate_layout) {
        R(31) = L->code + 0x174;
        c->budget -= 2;
        nc_call(c, L->code + 0xdb4);
        alternate_layout = R(2) == 2;
        c->budget -= 3;
    }

    const u32 depth = R(16) & 3, blend = R(18) & 3;
    const u32 page_x = (R(19) & 0x3ff) >> 6, y = R(17);
    if (alternate_layout) {
        R(3) = (depth << 9) | (blend << 7) | ((y & 0x300) >> 3);
        R(2) = R(3) | page_x;
        c->budget -= 11;
    } else {
        R(3) = (depth << 7) | (blend << 5) | ((y & 0x100) >> 4) | page_x;
        R(2) = R(3) | ((y & 0x200) << 2);
        c->budget -= 13;
    }

    R(31) = MEM_R32(R(29) + 0x20);
    c->budget -= NC_LOAD_CYCLES;
    for (int reg = 19; reg >= 16; reg--) {
        R(reg) = MEM_R32(R(29) + 0x10 + (reg - 16) * 4);
        c->budget -= NC_LOAD_CYCLES;
    }
    R(29) += 0x28;
    c->budget -= 3;
    return true;
}

static bool sdk_gpu_default_draw(CPUState *c, const NcGpuLayout *L) {
    if (c->budget <= 83)
        return false;

    R(29) -= 0x28;
    c->budget--;
    MEM_W32(R(29) + 0x18, R(18));
    c->budget--;
    R(18) = MEM_R32(R(29) + 0x38); /* height, the fifth argument */
    c->budget -= NC_LOAD_CYCLES;
    MEM_W32(R(29) + 0x14, R(17));
    R(17) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 0x1c, R(19));
    R(19) = R(5);
    c->budget -= 2;
    MEM_W32(R(29) + 0x20, R(20));
    R(20) = R(6);
    c->budget -= 2;
    MEM_W32(R(29) + 0x10, R(16));
    c->budget--;
    MEM_W32(R(29) + 0x24, R(31));
    R(31) = L->code + 0x34;
    R(16) = R(7);
    c->budget -= 3;
    nc_call(c, L->code + 0x124);

    const u32 env = R(17);
    bool pal = R(2) != 0;
    R(3) = 1;
    c->budget--;
    MEM_W16(env, (u16)R(19));
    c->budget--;
    MEM_W16(env + 2, (u16)R(20));
    c->budget--;
    MEM_W16(env + 4, (u16)R(16));
    c->budget--;
    /* Empty texture window and black background. */
    for (unsigned off = 0x0c; off <= 0x12; off += 2) {
        MEM_W16(env + off, 0);
        c->budget--;
    }
    for (unsigned off = 0x19; off <= 0x1b; off++) {
        MEM_W8(env + off, 0);
        c->budget--;
    }
    MEM_W8(env + 0x16, 1); /* dithering */
    c->budget--;
    MEM_W16(env + 6, (u16)R(18));
    R(2) = (s32)R(18) <= (pal ? 288 : 256);
    c->budget -= pal ? 4 : 3;
    MEM_W8(env + 0x17, (u8)R(2)); /* allow drawing to the displayed area */
    c->budget--;
    MEM_W16(env + 8, (u16)R(19));
    c->budget--;
    R(31) = L->code + 0x88;
    MEM_W16(env + 10, (u16)R(20));
    c->budget -= 2;
    nc_call(c, L->code + 0xdb4);
    bool read_again = R(2) != 1;
    R(2) = env;
    c->budget -= 3;
    if (read_again) {
        R(31) = L->code + 0x9c;
        c->budget -= 2;
        nc_call(c, L->code + 0xdb4);
        R(2) = env;
        c->budget--;
    }

    R(3) = 10;
    c->budget--;
    MEM_W16(env + 0x14, (u16)R(3)); /* texture page */
    c->budget--;
    MEM_W8(env + 0x18, 0); /* disable background clear */
    c->budget--;
    R(31) = MEM_R32(R(29) + 0x24);
    c->budget -= NC_LOAD_CYCLES;
    for (int reg = 20; reg >= 16; reg--) {
        R(reg) = MEM_R32(R(29) + 0x10 + (reg - 16) * 4);
        c->budget -= NC_LOAD_CYCLES;
    }
    R(29) += 0x28;
    c->budget -= 3;
    return true;
}

#endif

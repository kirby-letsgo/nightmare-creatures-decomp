#include "port/hw/hw.h"
#include "port/runtime.h"

/* Placeholder GPU: tracks command packets so the boot can be followed, renders nothing.
 * Replaced by the SDL_GPU renderer in Phase 3. */

static u32 pending;    /* remaining words of the current GP0 packet */
static u32 vram_words; /* remaining words of a CPU->VRAM upload */
static u32 frame;
static u64 draw_packets;
static bool odd_line;

static u32 packet_len(u32 cmd) {
    u32 op = cmd >> 24;
    switch (op >> 5) {
    case 1: { /* polygon */
        u32 verts = (op & 0x08) ? 4 : 3;
        u32 per = 1 + ((op & 0x04) ? 1 : 0) + ((op & 0x10) ? 1 : 0);
        return 1 + verts * per - ((op & 0x10) ? 1 : 0);
    }
    case 2: /* line (polylines terminate with 0x5xxx5xxx; treated as fixed here) */
        return (op & 0x10) ? 4 : 3;
    case 3: { /* rectangle */
        u32 n = 2 + ((op & 0x04) ? 1 : 0);
        return ((op >> 3) & 3) == 0 ? n + 1 : n;
    }
    case 4:
        return 4; /* VRAM->VRAM copy */
    case 5:
    case 6:
        return 3; /* CPU<->VRAM transfer header */
    default:
        return op == 0x02 ? 3 : 1; /* fill rectangle, or environment command */
    }
}

void gpu_gp0(u32 word) {
    if (vram_words > 0) {
        vram_words--;
        return;
    }
    static u32 cmd, idx;
    static u32 xfer_size;
    if (pending == 0) {
        cmd = word;
        idx = 0;
        pending = packet_len(word);
        u32 op = word >> 24;
        if (op >= 0x20 && op < 0x80) {
            if (draw_packets++ == 0) {
                NC_LOG("gpu: first draw packet 0x%08X (frame %u)", word, frame);
            }
        }
    }
    if (idx == 2 && (cmd >> 29) == 5) {
        xfer_size = word;
    }
    idx++;
    if (--pending == 0 && (cmd >> 29) == 5) {
        u32 w = xfer_size & 0xFFFF, h = xfer_size >> 16;
        w = w ? w : 1024;
        h = h ? h : 512;
        vram_words = (w * h + 1) / 2;
    }
}

void gpu_gp1(u32 word) {
    if ((word >> 24) == 0x00 || (word >> 24) == 0x01) {
        pending = 0;
        vram_words = 0;
    }
}

u32 gpu_read(void) {
    return 0;
}

u32 gpu_status(void) {
    /* Ready for commands, VRAM->CPU and DMA; interlace odd/even line toggles each frame. */
    return 0x1C000000u | (odd_line ? 0x80000000u : 0u);
}

void gpu_vblank(void) {
    frame++;
    odd_line = !odd_line;
    if (frame % 60 == 0) {
        NC_LOG("gpu: frame %u, %llu draw packets so far", frame, (unsigned long long)draw_packets);
    }
}

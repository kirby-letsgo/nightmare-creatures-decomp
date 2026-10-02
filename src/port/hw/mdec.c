/* MDEC (motion decoder) at 0x1F801820/0x1F801824: decodes run-length coded macroblocks
 * (as produced by Psy-Q's DecDCTvlc for STR movies) into 15/24-bit pixels.
 * Reference: psx-spx "Macroblock Decoder (MDEC)". Commands complete instantly. */
#include "port/hw/hw.h"
#include "port/runtime.h"

#include <string.h>

#define IN_MAX (0x10000u * 2u)       /* halfwords of RLE input */
#define OUT_MAX (0x10000u * 4u * 2u) /* bytes of decoded output */

typedef struct MdecState {
    u32 cmd;
    u32 words_left; /* parameter words still expected for the current command */
    u16 in[IN_MAX];
    u32 in_len;
    u8 qt_y[64], qt_c[64];
    s16 scale[64];
    u8 out[OUT_MAX];
    u32 out_len, out_pos;
    bool dma_in, dma_out;
} MdecState;

static MdecState md;

/* Sequence index -> 8x8 matrix position. */
static const u8 zagzig[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static int signed10(u32 v) {
    return (int)((v & 0x3FFu) ^ 0x200u) - 0x200;
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void idct(int blk[64]) {
    int tmp[64];
    int *src = blk, *dst = tmp;
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                s64 sum = 0;
                for (int z = 0; z < 8; z++) {
                    sum += (s64)src[y + z * 8] * (md.scale[x + z * 8] / 8);
                }
                dst[x + y * 8] = (int)((sum + 0xFFF) / 0x2000);
            }
        }
        int *t = src;
        src = dst;
        dst = t;
    }
}

/* Decodes one 8x8 block from the RLE stream at *pos. Returns false at the end of input. */
static bool decode_block(int blk[64], u32 *pos, const u8 *qt) {
    memset(blk, 0, 64 * sizeof blk[0]);
    u32 n;
    do {
        if (*pos >= md.in_len) {
            return false;
        }
        n = md.in[(*pos)++];
    } while (n == 0xFE00u);

    u32 k = 0;
    int q_scale = (int)((n >> 10) & 0x3F);
    int val = signed10(n) * qt[0];
    for (;;) {
        if (q_scale == 0) {
            val = signed10(n) * 2;
        }
        val = clamp(val, -0x400, 0x3FF);
        blk[q_scale > 0 ? zagzig[k] : k] = val;
        if (*pos >= md.in_len) {
            break;
        }
        n = md.in[(*pos)++];
        k += ((n >> 10) & 0x3F) + 1;
        if (k > 63) {
            break;
        }
        val = (signed10(n) * qt[k] * q_scale + 4) / 8;
    }
    idct(blk);
    return true;
}

static void put_pixel(u32 depth, bool sign, bool bit15, int r, int g, int b) {
    if (!sign) {
        r ^= 0x80;
        g ^= 0x80;
        b ^= 0x80;
    }
    if (depth == 2) { /* 24-bit */
        if (md.out_len + 3 <= OUT_MAX) {
            md.out[md.out_len++] = (u8)r;
            md.out[md.out_len++] = (u8)g;
            md.out[md.out_len++] = (u8)b;
        }
    } else { /* 15-bit */
        u16 px = (u16)(((r & 0xFF) >> 3) | (((g & 0xFF) >> 3) << 5) | (((b & 0xFF) >> 3) << 10) |
                       (bit15 ? 0x8000 : 0));
        if (md.out_len + 2 <= OUT_MAX) {
            md.out[md.out_len++] = (u8)px;
            md.out[md.out_len++] = (u8)(px >> 8);
        }
    }
}

static void decode_color(u32 depth, bool sign, bool bit15) {
    u32 pos = 0;
    int cr[64], cb[64], y[4][64];
    for (;;) {
        if (!decode_block(cr, &pos, md.qt_c) || !decode_block(cb, &pos, md.qt_c)) {
            return;
        }
        for (int i = 0; i < 4; i++) {
            if (!decode_block(y[i], &pos, md.qt_y)) {
                return;
            }
        }
        for (int py = 0; py < 16; py++) {
            for (int px = 0; px < 16; px++) {
                int ci = (px / 2) + (py / 2) * 8;
                double R = cr[ci], B = cb[ci];
                double G = -0.3437 * B - 0.7143 * R;
                R *= 1.402;
                B *= 1.772;
                int lum = y[(py / 8) * 2 + (px / 8)][(px % 8) + (py % 8) * 8];
                put_pixel(depth, sign, bit15, clamp(lum + (int)R, -128, 127),
                          clamp(lum + (int)G, -128, 127), clamp(lum + (int)B, -128, 127));
            }
        }
    }
}

static void decode_mono(u32 depth, bool sign) {
    u32 pos = 0;
    int blk[64];
    while (decode_block(blk, &pos, md.qt_y)) {
        for (int i = 0; i < 64; i++) {
            int v = clamp(blk[i], -128, 127);
            if (!sign) {
                v ^= 0x80;
            }
            if (depth == 1) { /* 8-bit */
                md.out[md.out_len++] = (u8)v;
            } else if (i % 2 == 0) { /* 4-bit: two pixels per byte */
                md.out[md.out_len] = (u8)((v & 0xFF) >> 4);
            } else {
                md.out[md.out_len++] |= (u8)(((v & 0xFF) >> 4) << 4);
            }
        }
    }
}

static void run_command(void) {
    u32 op = md.cmd >> 29;
    switch (op) {
    case 1: {
        u32 depth = (md.cmd >> 27) & 3;
        bool sign = (md.cmd >> 26) & 1;
        bool bit15 = (md.cmd >> 25) & 1;
        md.out_len = md.out_pos = 0;
        if (depth >= 2) {
            decode_color(depth == 3 ? 3 : 2, sign, bit15);
        } else {
            decode_mono(depth, sign);
        }
        break;
    }
    case 2: { /* quant tables: 64 bytes luminance (+ 64 bytes colour) */
        const u8 *bytes = (const u8 *)md.in;
        memcpy(md.qt_y, bytes, 64);
        if (md.cmd & 1) {
            memcpy(md.qt_c, bytes + 64, 64);
        }
        break;
    }
    case 3: /* scale table: 64 signed halfwords */
        memcpy(md.scale, md.in, sizeof md.scale);
        break;
    default:
        break;
    }
}

static void write_param(u32 word) {
    if (md.words_left == 0) {
        md.cmd = word;
        md.in_len = 0;
        switch (word >> 29) {
        case 1:
            md.words_left = word & 0xFFFFu;
            break;
        case 2:
            md.words_left = (word & 1) ? 32 : 16;
            break;
        case 3:
            md.words_left = 32;
            break;
        default:
            md.words_left = 0;
            break;
        }
        if (md.words_left == 0) {
            run_command();
        }
        return;
    }
    if (md.in_len + 2 <= IN_MAX) {
        md.in[md.in_len++] = (u16)word;
        md.in[md.in_len++] = (u16)(word >> 16);
    }
    if (--md.words_left == 0) {
        run_command();
    }
}

u32 mdec_read(u32 reg) {
    if (reg == 0) {
        u32 w = 0;
        for (int i = 0; i < 4; i++) {
            u8 b = md.out_pos < md.out_len ? md.out[md.out_pos++] : 0;
            w |= (u32)b << (8 * i);
        }
        return w;
    }
    u32 status = 0;
    if (md.out_pos >= md.out_len) {
        status |= 1u << 31; /* data-out FIFO empty */
    }
    if (md.words_left > 0) {
        status |= 1u << 29; /* busy receiving parameters */
        status |= md.dma_in ? 1u << 28 : 0;
    }
    if (md.out_pos < md.out_len && md.dma_out) {
        status |= 1u << 27;
    }
    status |= ((md.cmd >> 25) & 0xFu) << 23;
    status |= (md.words_left - 1u) & 0xFFFFu;
    return status;
}

void mdec_write(u32 reg, u32 value) {
    if (reg == 0) {
        write_param(value);
        return;
    }
    if (value & 0x80000000u) {
        md.words_left = 0;
        md.out_len = md.out_pos = 0;
        md.in_len = 0;
    }
    md.dma_in = (value >> 30) & 1;
    md.dma_out = (value >> 29) & 1;
}

void mdec_dma_write(const u8 *src, u32 bytes) {
    for (u32 i = 0; i + 4 <= bytes; i += 4) {
        write_param((u32)src[i] | (u32)src[i + 1] << 8 | (u32)src[i + 2] << 16 |
                    (u32)src[i + 3] << 24);
    }
}

void mdec_dma_read(u8 *dst, u32 bytes) {
    for (u32 i = 0; i < bytes; i++) {
        dst[i] = md.out_pos < md.out_len ? md.out[md.out_pos++] : 0;
    }
}

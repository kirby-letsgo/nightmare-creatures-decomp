/* GTE (COP2) geometry transformation engine.
 * Reference: psx-spx "Geometry Transformation Engine (GTE)". Results follow the hardware's
 * fixed-point behaviour, including its UNR division for perspective projection, the
 * saturation flags and the register read/write quirks. */
#include "port/runtime.h"

#include <string.h>

#define D (c->gte.data)
#define C (c->gte.ctrl)

/* --- register helpers --------------------------------------------------------------------- */

static s32 lo16(u32 v) {
    return (s16)(v & 0xFFFF);
}
static s32 hi16(u32 v) {
    return (s16)(v >> 16);
}

enum {
    F_MAC1_POS = 1u << 30,
    F_MAC2_POS = 1u << 29,
    F_MAC3_POS = 1u << 28,
    F_MAC1_NEG = 1u << 27,
    F_MAC2_NEG = 1u << 26,
    F_MAC3_NEG = 1u << 25,
    F_IR1 = 1u << 24,
    F_IR2 = 1u << 23,
    F_IR3 = 1u << 22,
    F_R = 1u << 21,
    F_G = 1u << 20,
    F_B = 1u << 19,
    F_SZ = 1u << 18,
    F_DIV = 1u << 17,
    F_MAC0_POS = 1u << 16,
    F_MAC0_NEG = 1u << 15,
    F_SX = 1u << 14,
    F_SY = 1u << 13,
    F_IR0 = 1u << 12,
};

/* Matrices and vectors as signed 16-bit elements. */
typedef struct Mat {
    s32 m[3][3];
} Mat;

static Mat matrix(CPUState *c, int base) {
    Mat r;
    r.m[0][0] = lo16(C[base + 0]);
    r.m[0][1] = hi16(C[base + 0]);
    r.m[0][2] = lo16(C[base + 1]);
    r.m[1][0] = hi16(C[base + 1]);
    r.m[1][1] = lo16(C[base + 2]);
    r.m[1][2] = hi16(C[base + 2]);
    r.m[2][0] = lo16(C[base + 3]);
    r.m[2][1] = hi16(C[base + 3]);
    r.m[2][2] = lo16(C[base + 4]);
    return r;
}

static void vertex(CPUState *c, int n, s32 v[3]) {
    v[0] = lo16(D[n * 2]);
    v[1] = hi16(D[n * 2]);
    v[2] = lo16(D[n * 2 + 1]);
}

static s32 ir(CPUState *c, int i) {
    return lo16(D[8 + i]);
}

/* --- flag-checked arithmetic ------------------------------------------------------------ */

/* MAC1-3 intermediate results are 44-bit; out-of-range values set the overflow flags. */
static s64 mac44(CPUState *c, int i, s64 v) {
    static const u32 pos[3] = {F_MAC1_POS, F_MAC2_POS, F_MAC3_POS};
    static const u32 neg[3] = {F_MAC1_NEG, F_MAC2_NEG, F_MAC3_NEG};
    if (v > 0x7FFFFFFFFFFLL) {
        c->gte.flag |= pos[i - 1];
    } else if (v < -0x80000000000LL) {
        c->gte.flag |= neg[i - 1];
    }
    /* sign-extend from 44 bits */
    return (s64)((u64)v << 20) >> 20;
}

static void set_mac(CPUState *c, int i, s64 v, int shift) {
    D[24 + i] = (u32)(s32)(mac44(c, i, v) >> shift);
}

static s32 mac(CPUState *c, int i) {
    return (s32)D[24 + i];
}

static void set_mac0(CPUState *c, s64 v) {
    if (v > 0x7FFFFFFFLL) {
        c->gte.flag |= F_MAC0_POS;
    } else if (v < -0x80000000LL) {
        c->gte.flag |= F_MAC0_NEG;
    }
    D[24] = (u32)(s32)v;
}

static void set_ir(CPUState *c, int i, s32 v, bool lm) {
    static const u32 bits[3] = {F_IR1, F_IR2, F_IR3};
    s32 lo = lm ? 0 : -0x8000;
    if (v < lo) {
        v = lo;
        c->gte.flag |= bits[i - 1];
    } else if (v > 0x7FFF) {
        v = 0x7FFF;
        c->gte.flag |= bits[i - 1];
    }
    D[8 + i] = (u32)(s32)v;
}

static void set_ir0(CPUState *c, s64 v) {
    if (v < 0) {
        v = 0;
        c->gte.flag |= F_IR0;
    } else if (v > 0x1000) {
        v = 0x1000;
        c->gte.flag |= F_IR0;
    }
    D[8] = (u32)(s32)v;
}

static void mac_to_ir(CPUState *c, bool lm) {
    set_ir(c, 1, mac(c, 1), lm);
    set_ir(c, 2, mac(c, 2), lm);
    set_ir(c, 3, mac(c, 3), lm);
}

static u8 sat_color(CPUState *c, s32 v, u32 flag) {
    if (v < 0) {
        c->gte.flag |= flag;
        return 0;
    }
    if (v > 0xFF) {
        c->gte.flag |= flag;
        return 0xFF;
    }
    return (u8)v;
}

static void push_color(CPUState *c) {
    u8 code = (u8)(D[6] >> 24);
    u8 r = sat_color(c, mac(c, 1) >> 4, F_R);
    u8 g = sat_color(c, mac(c, 2) >> 4, F_G);
    u8 b = sat_color(c, mac(c, 3) >> 4, F_B);
    D[20] = D[21];
    D[21] = D[22];
    D[22] = (u32)r | (u32)g << 8 | (u32)b << 16 | (u32)code << 24;
}

static void push_sz(CPUState *c, s64 v) {
    if (v < 0) {
        v = 0;
        c->gte.flag |= F_SZ;
    } else if (v > 0xFFFF) {
        v = 0xFFFF;
        c->gte.flag |= F_SZ;
    }
    D[16] = D[17];
    D[17] = D[18];
    D[18] = D[19];
    D[19] = (u32)v;
}

static s32 sat_sxy(CPUState *c, s64 v, u32 flag) {
    if (v < -0x400) {
        c->gte.flag |= flag;
        return -0x400;
    }
    if (v > 0x3FF) {
        c->gte.flag |= flag;
        return 0x3FF;
    }
    return (s32)v;
}

static void push_sxy(CPUState *c, s32 x, s32 y) {
    D[12] = D[13];
    D[13] = D[14];
    D[14] = ((u32)x & 0xFFFF) | ((u32)y << 16);
}

/* --- division (UNR) ----------------------------------------------------------------------- */

static u8 unr_table[0x101];

static void init_unr(void) {
    static bool done;
    if (done) {
        return;
    }
    for (int i = 0; i < 0x101; i++) {
        int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        unr_table[i] = (u8)(v < 0 ? 0 : v);
    }
    done = true;
}

static int clz16(u32 v) {
    int n = 0;
    for (u32 bit = 0x8000; bit != 0 && !(v & bit); bit >>= 1) {
        n++;
    }
    return n;
}

static u32 divide(CPUState *c, u32 h, u32 sz3) {
    if (h < sz3 * 2) {
        int z = clz16(sz3);
        u64 n = (u64)h << z;
        u32 d = sz3 << z;
        u32 u = unr_table[(d - 0x7FC0) >> 7] + 0x101u;
        d = (u32)((0x2000080u - (u64)d * u) >> 8);
        d = (u32)((0x0000080u + (u64)d * u) >> 8);
        u64 q = (n * d + 0x8000u) >> 16;
        return q > 0x1FFFF ? 0x1FFFF : (u32)q;
    }
    c->gte.flag |= F_DIV;
    return 0x1FFFF;
}

/* --- commands ----------------------------------------------------------------------------- */

/* Widescreen: perspective X is scaled by 3/4 so a 4:3 frame shown at 16:9 has correct
 * proportions (and a wider field of view). Set from settings by gte_set_widescreen(). */
static bool widescreen;

void gte_set_widescreen(bool on) {
    widescreen = on;
}

static void rtp(CPUState *c, int n, int sf, bool lm, bool last) {
    s32 v[3];
    vertex(c, n, v);
    Mat rt = matrix(c, 0);
    s64 tr[3] = {(s32)C[5], (s32)C[6], (s32)C[7]};
    s64 m[3];
    for (int i = 0; i < 3; i++) {
        m[i] = mac44(c, i + 1, (tr[i] << 12) + (s64)rt.m[i][0] * v[0]);
        m[i] = mac44(c, i + 1, m[i] + (s64)rt.m[i][1] * v[1]);
        m[i] = mac44(c, i + 1, m[i] + (s64)rt.m[i][2] * v[2]);
        D[25 + i] = (u32)(s32)(m[i] >> (sf * 12));
    }
    set_ir(c, 1, mac(c, 1), lm);
    set_ir(c, 2, mac(c, 2), lm);
    /* IR3's saturation flag is computed from the unshifted-by-sf value (hardware quirk). */
    s32 ir3 = mac(c, 3);
    s32 ir3_lo = lm ? 0 : -0x8000;
    D[11] = (u32)(ir3 < ir3_lo ? ir3_lo : (ir3 > 0x7FFF ? 0x7FFF : ir3));
    s64 raw3 = m[2] >> 12;
    if (raw3 < -0x8000 || raw3 > 0x7FFF) {
        c->gte.flag |= F_IR3;
    }

    push_sz(c, m[2] >> 12);
    u32 q = divide(c, (u16)C[26], D[19]);
    s64 px = (s64)q * ir(c, 1);
    s64 sx = (widescreen ? px * 3 / 4 : px) + (s32)C[24];
    s64 sy = (s64)q * ir(c, 2) + (s32)C[25];
    set_mac0(c, sx);
    set_mac0(c, sy);
    push_sxy(c, sat_sxy(c, sx >> 16, F_SX), sat_sxy(c, sy >> 16, F_SY));
    if (last) {
        s64 dq = (s64)q * lo16(C[27]) + (s32)C[28];
        set_mac0(c, dq);
        set_ir0(c, dq >> 12);
    }
}

static void nclip(CPUState *c) {
    s64 x0 = lo16(D[12]), y0 = hi16(D[12]);
    s64 x1 = lo16(D[13]), y1 = hi16(D[13]);
    s64 x2 = lo16(D[14]), y2 = hi16(D[14]);
    set_mac0(c, x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1);
}

static void avsz(CPUState *c, bool four) {
    s64 sum = (s64)(u16)D[17] + (u16)D[18] + (u16)D[19];
    s64 v;
    if (four) {
        v = (s64)lo16(C[30]) * (sum + (u16)D[16]);
    } else {
        v = (s64)lo16(C[29]) * sum;
    }
    set_mac0(c, v);
    s64 otz = v >> 12;
    if (otz < 0) {
        otz = 0;
        c->gte.flag |= F_SZ;
    } else if (otz > 0xFFFF) {
        otz = 0xFFFF;
        c->gte.flag |= F_SZ;
    }
    D[7] = (u32)otz;
}

/* MAC = matrix * vector (+ translation), with the hardware's FC-translation bug for cv=2. */
static void mat_vec(CPUState *c, const Mat *m, const s32 v[3], const s64 tr[3], int sf, bool lm,
                    bool fc_bug) {
    for (int i = 0; i < 3; i++) {
        s64 acc;
        if (fc_bug) {
            /* The first product goes through a saturation step whose result is discarded. */
            acc = mac44(c, i + 1, (tr[i] << 12) + (s64)m->m[i][0] * v[0]);
            set_ir(c, i + 1, (s32)(acc >> (sf * 12)), false);
            acc = mac44(c, i + 1, (s64)m->m[i][1] * v[1]);
            acc = mac44(c, i + 1, acc + (s64)m->m[i][2] * v[2]);
        } else {
            acc = mac44(c, i + 1, (tr[i] << 12) + (s64)m->m[i][0] * v[0]);
            acc = mac44(c, i + 1, acc + (s64)m->m[i][1] * v[1]);
            acc = mac44(c, i + 1, acc + (s64)m->m[i][2] * v[2]);
        }
        D[25 + i] = (u32)(s32)(acc >> (sf * 12));
    }
    mac_to_ir(c, lm);
}

static void mvmva(CPUState *c, u32 cmd, int sf, bool lm) {
    int mx = (int)((cmd >> 17) & 3), vi = (int)((cmd >> 15) & 3), cv = (int)((cmd >> 13) & 3);
    Mat m;
    if (mx == 3) { /* garbage matrix as documented */
        s32 r = (s32)((D[6] & 0xFF) << 4);
        memset(&m, 0, sizeof m);
        m.m[0][0] = -r;
        m.m[0][1] = r;
        m.m[0][2] = lo16(C[2]);
        m.m[1][0] = m.m[1][1] = m.m[1][2] = lo16(C[1]);
        m.m[2][0] = m.m[2][1] = m.m[2][2] = lo16(C[4]);
    } else {
        m = matrix(c, mx * 8);
    }
    s32 v[3];
    if (vi == 3) {
        v[0] = ir(c, 1);
        v[1] = ir(c, 2);
        v[2] = ir(c, 3);
    } else {
        vertex(c, vi, v);
    }
    s64 tr[3] = {0, 0, 0};
    if (cv < 3) {
        int base = cv == 0 ? 5 : (cv == 1 ? 13 : 21);
        tr[0] = (s32)C[base];
        tr[1] = (s32)C[base + 1];
        tr[2] = (s32)C[base + 2];
    }
    mat_vec(c, &m, v, tr, sf, lm, cv == 2);
}

/* Interpolates MAC towards the far colour by IR0 (depth cueing); MAC holds values << 12. */
static void depth_cue(CPUState *c, s64 m[3], int sf, bool lm) {
    s64 fc[3] = {(s32)C[21], (s32)C[22], (s32)C[23]};
    for (int i = 0; i < 3; i++) {
        s64 diff = mac44(c, i + 1, (fc[i] << 12) - m[i]);
        set_ir(c, i + 1, (s32)(diff >> (sf * 12)), false);
    }
    for (int i = 0; i < 3; i++) {
        m[i] = mac44(c, i + 1, (s64)ir(c, i + 1) * lo16(D[8]) + m[i]);
        D[25 + i] = (u32)(s32)(m[i] >> (sf * 12));
    }
    mac_to_ir(c, lm);
}

/* Light the normal in vertex n: IR = LLM*V, then IR = BK + LCM*IR. */
static void light(CPUState *c, int n, int sf, bool lm) {
    s32 v[3];
    vertex(c, n, v);
    Mat llm = matrix(c, 8), lcm = matrix(c, 16);
    s64 zero[3] = {0, 0, 0};
    mat_vec(c, &llm, v, zero, sf, lm, false);
    s32 irv[3] = {ir(c, 1), ir(c, 2), ir(c, 3)};
    s64 bk[3] = {(s32)C[13], (s32)C[14], (s32)C[15]};
    mat_vec(c, &lcm, irv, bk, sf, lm, false);
}

/* Colour stage shared by NCC/NCD/CC/CDP: MAC = RGBC * IR << 4, optionally depth-cued. */
static void color_stage(CPUState *c, int sf, bool lm, bool cue) {
    s64 rgb[3] = {D[6] & 0xFF, (D[6] >> 8) & 0xFF, (D[6] >> 16) & 0xFF};
    s64 m[3];
    for (int i = 0; i < 3; i++) {
        m[i] = mac44(c, i + 1, (rgb[i] * ir(c, i + 1)) << 4);
    }
    if (cue) {
        depth_cue(c, m, sf, lm);
    } else {
        for (int i = 0; i < 3; i++) {
            D[25 + i] = (u32)(s32)(m[i] >> (sf * 12));
        }
        mac_to_ir(c, lm);
    }
    push_color(c);
}

static void ncs(CPUState *c, int n, int sf, bool lm) {
    light(c, n, sf, lm);
    push_color(c);
}

static void dpc(CPUState *c, u32 rgb, int sf, bool lm) {
    s64 m[3] = {(s64)(rgb & 0xFF) << 16, (s64)((rgb >> 8) & 0xFF) << 16,
                (s64)((rgb >> 16) & 0xFF) << 16};
    depth_cue(c, m, sf, lm);
    push_color(c);
}

void gte_command(CPUState *c, u32 cmd) {
    init_unr();
    c->gte.flag = 0;
    int sf = (cmd >> 19) & 1 ? 1 : 0;
    bool lm = (cmd >> 10) & 1;
    switch (cmd & 0x3F) {
    case 0x01: /* RTPS */
        rtp(c, 0, sf, lm, true);
        break;
    case 0x06:
        nclip(c);
        break;
    case 0x0C: { /* OP */
        s64 d1 = lo16(C[0]), d2 = lo16(C[2]), d3 = lo16(C[4]);
        s64 i1 = ir(c, 1), i2 = ir(c, 2), i3 = ir(c, 3);
        set_mac(c, 1, i3 * d2 - i2 * d3, sf * 12);
        set_mac(c, 2, i1 * d3 - i3 * d1, sf * 12);
        set_mac(c, 3, i2 * d1 - i1 * d2, sf * 12);
        mac_to_ir(c, lm);
        break;
    }
    case 0x10: /* DPCS */
        dpc(c, D[6], sf, lm);
        break;
    case 0x11: { /* INTPL */
        s64 m[3] = {(s64)ir(c, 1) << 12, (s64)ir(c, 2) << 12, (s64)ir(c, 3) << 12};
        depth_cue(c, m, sf, lm);
        push_color(c);
        break;
    }
    case 0x12:
        mvmva(c, cmd, sf, lm);
        break;
    case 0x13: /* NCDS */
        light(c, 0, sf, lm);
        color_stage(c, sf, lm, true);
        break;
    case 0x14: { /* CDP */
        s32 irv[3] = {ir(c, 1), ir(c, 2), ir(c, 3)};
        Mat lcm = matrix(c, 16);
        s64 bk[3] = {(s32)C[13], (s32)C[14], (s32)C[15]};
        mat_vec(c, &lcm, irv, bk, sf, lm, false);
        color_stage(c, sf, lm, true);
        break;
    }
    case 0x16: /* NCDT */
        for (int n = 0; n < 3; n++) {
            light(c, n, sf, lm);
            color_stage(c, sf, lm, true);
        }
        break;
    case 0x1B: /* NCCS */
        light(c, 0, sf, lm);
        color_stage(c, sf, lm, false);
        break;
    case 0x1C: { /* CC */
        s32 irv[3] = {ir(c, 1), ir(c, 2), ir(c, 3)};
        Mat lcm = matrix(c, 16);
        s64 bk[3] = {(s32)C[13], (s32)C[14], (s32)C[15]};
        mat_vec(c, &lcm, irv, bk, sf, lm, false);
        color_stage(c, sf, lm, false);
        break;
    }
    case 0x1E: /* NCS */
        ncs(c, 0, sf, lm);
        break;
    case 0x20: /* NCT */
        for (int n = 0; n < 3; n++) {
            ncs(c, n, sf, lm);
        }
        break;
    case 0x28: /* SQR */
        for (int i = 1; i <= 3; i++) {
            set_mac(c, i, (s64)ir(c, i) * ir(c, i), sf * 12);
        }
        mac_to_ir(c, lm);
        break;
    case 0x29: { /* DCPL */
        s64 rgb[3] = {D[6] & 0xFF, (D[6] >> 8) & 0xFF, (D[6] >> 16) & 0xFF};
        s64 m[3];
        for (int i = 0; i < 3; i++) {
            m[i] = (rgb[i] * ir(c, i + 1)) << 4;
        }
        depth_cue(c, m, sf, lm);
        push_color(c);
        break;
    }
    case 0x2A: /* DPCT: three times on the colour FIFO's oldest entry */
        for (int n = 0; n < 3; n++) {
            dpc(c, D[20], sf, lm);
        }
        break;
    case 0x2D:
        avsz(c, false);
        break;
    case 0x2E:
        avsz(c, true);
        break;
    case 0x30: /* RTPT */
        rtp(c, 0, sf, lm, false);
        rtp(c, 1, sf, lm, false);
        rtp(c, 2, sf, lm, true);
        break;
    case 0x3D: /* GPF */
        for (int i = 1; i <= 3; i++) {
            set_mac(c, i, (s64)ir(c, i) * lo16(D[8]), sf * 12);
        }
        mac_to_ir(c, lm);
        push_color(c);
        break;
    case 0x3E: /* GPL */
        for (int i = 1; i <= 3; i++) {
            s64 base = (s64)mac(c, i) << (sf * 12);
            set_mac(c, i, (s64)ir(c, i) * lo16(D[8]) + base, sf * 12);
        }
        mac_to_ir(c, lm);
        push_color(c);
        break;
    case 0x3F: /* NCCT */
        for (int n = 0; n < 3; n++) {
            light(c, n, sf, lm);
            color_stage(c, sf, lm, false);
        }
        break;
    default:
        NC_LOG("GTE: unknown command 0x%07X", cmd);
        break;
    }
    if (c->gte.flag & 0x7F87E000u) {
        c->gte.flag |= 0x80000000u;
    }
}

/* --- register access ---------------------------------------------------------------------- */

static u32 sat5(s32 v) {
    v >>= 7;
    return (u32)(v < 0 ? 0 : (v > 0x1F ? 0x1F : v));
}

u32 gte_read_data(CPUState *c, int reg) {
    switch (reg) {
    case 1:
    case 3:
    case 5:
    case 8:
    case 9:
    case 10:
    case 11:
        return (u32)lo16(D[reg]);
    case 7:
    case 16:
    case 17:
    case 18:
    case 19:
        return D[reg] & 0xFFFF;
    case 15:
        return D[14];
    case 28:
    case 29:
        return sat5(ir(c, 1)) | sat5(ir(c, 2)) << 5 | sat5(ir(c, 3)) << 10;
    case 31: {
        u32 v = D[30];
        u32 n = 0;
        u32 sign = v >> 31;
        for (int b = 31; b >= 0 && ((v >> b) & 1) == sign; b--) {
            n++;
        }
        return n;
    }
    default:
        return D[reg & 31];
    }
}

void gte_write_data(CPUState *c, int reg, u32 value) {
    switch (reg) {
    case 15: /* SXYP: pushes the screen XY FIFO */
        D[12] = D[13];
        D[13] = D[14];
        D[14] = value;
        break;
    case 28: /* IRGB: expands into IR1-3 */
        D[28] = value & 0x7FFF;
        D[9] = (value & 0x1F) << 7;
        D[10] = ((value >> 5) & 0x1F) << 7;
        D[11] = ((value >> 10) & 0x1F) << 7;
        break;
    case 29:
    case 31: /* read-only */
        break;
    default:
        D[reg & 31] = value;
        break;
    }
}

u32 gte_read_ctrl(CPUState *c, int reg) {
    switch (reg) {
    case 4:
    case 12:
    case 20:
    case 26: /* H reads back sign-extended (hardware quirk) */
    case 27:
    case 29:
    case 30:
        return (u32)lo16(C[reg]);
    case 31:
        return c->gte.flag;
    default:
        return C[reg & 31];
    }
}

void gte_write_ctrl(CPUState *c, int reg, u32 value) {
    if (reg == 31) {
        c->gte.flag = value & 0x7FFFF000u;
        if (c->gte.flag & 0x7F87E000u) {
            c->gte.flag |= 0x80000000u;
        }
        return;
    }
    C[reg & 31] = value;
}

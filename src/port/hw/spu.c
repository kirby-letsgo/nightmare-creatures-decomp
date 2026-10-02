/* SPU: 24 ADPCM voices with ADSR envelopes, pitch (and pitch modulation), noise, plus CD audio
 * (CD-DA and XA-ADPCM from the CD-ROM controller), mixed at 44.1 kHz.
 * Reference: psx-spx "Sound Processing Unit (SPU)" and "CDROM XA Audio ADPCM Compression".
 * Interpolation uses a cubic filter instead of the hardware's Gaussian table. Reverb is not
 * implemented yet (dry mix only). */
#include "port/hw/spu.h"

#include "port/hw/hw.h"
#include "port/runtime.h"

#include <string.h>

#define SPU_RAM_SIZE 0x80000u
#define VOICES 24
#define CD_RING 65536u /* stereo frames of CD audio buffered for mixing */

static const int adpcm_pos[5] = {0, 60, 115, 98, 122};
static const int adpcm_neg[5] = {0, 0, -52, -55, -60};

typedef enum Phase { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } Phase;

typedef struct Voice {
    u32 addr;        /* current ADPCM block (bytes) */
    u32 repeat_addr; /* loop start */
    u32 counter;     /* pitch counter: 12-bit fraction of the sample position */
    s16 decoded[28];
    s16 history[4]; /* last samples of the previous block, for interpolation */
    s32 prev1, prev2;
    int block_pos; /* sample index within decoded[] */
    Phase phase;
    s32 level;    /* envelope 0..0x7FFF */
    s32 env_wait; /* cycles remaining before the next envelope step */
    s32 last_out; /* for pitch modulation */
} Voice;

typedef struct SpuState {
    u16 regs[0x200];
    u8 ram[SPU_RAM_SIZE];
    u32 transfer_addr;
    Voice voices[VOICES];
    u32 endx;
    u32 noise_lfsr;
    s32 noise_wait;
    s16 noise_level;
    /* CD audio ring (stereo, 44.1 kHz) */
    s16 cd_ring[CD_RING][2];
    u32 cd_read, cd_write;
    /* XA decoder state */
    s32 xa_prev[2][2];
    u32 xa_phase; /* resampler phase (16.16) */
    s16 xa_last[2];
} SpuState;

static SpuState spu = {.noise_lfsr = 1};

/* Mixer gains set by the settings (0..256 = 0..100%). */
static int gain_master = 256, gain_music = 256, gain_sfx = 256;

void spu_set_gains(int master, int music, int sfx) {
    gain_master = master;
    gain_music = music;
    gain_sfx = sfx;
}

/* --- registers ---------------------------------------------------------------------------- */

#define REG(off) (spu.regs[((off) >> 1) & 0x1FF])
#define VREG(v, n) (spu.regs[((v) * 0x10 + (n)) >> 1])

enum {
    R_MAIN_VOL_L = 0x180,
    R_MAIN_VOL_R = 0x182,
    R_KON = 0x188,
    R_KOFF = 0x18C,
    R_PMON = 0x190,
    R_NON = 0x194,
    R_ENDX = 0x19C,
    R_TRANSFER_ADDR = 0x1A6,
    R_FIFO = 0x1A8,
    R_SPUCNT = 0x1AA,
    R_SPUSTAT = 0x1AE,
    R_CD_VOL_L = 0x1B0,
    R_CD_VOL_R = 0x1B2,
};

static u32 regs_pair(u32 off) {
    return REG(off) | (u32)REG(off + 2) << 16;
}

static void key_on(int v);
static void key_off(int v);

u16 spu_read(u32 reg) {
    reg &= 0x3FE;
    if (reg < 0x180 && (reg & 0xF) == 0xC) { /* current ADSR volume */
        return (u16)spu.voices[reg >> 4].level;
    }
    switch (reg) {
    case R_ENDX:
        return (u16)spu.endx;
    case R_ENDX + 2:
        return (u16)(spu.endx >> 16);
    case R_SPUSTAT:
        return REG(R_SPUCNT) & 0x3F; /* never busy */
    default:
        return REG(reg);
    }
}

void spu_write(u32 reg, u16 value) {
    reg &= 0x3FE;
    REG(reg) = value;
    switch (reg) {
    case R_KON:
    case R_KON + 2: {
        u32 bits = (u32)value << (reg == R_KON ? 0 : 16);
        for (int v = 0; v < VOICES; v++) {
            if (bits & (1u << v)) {
                key_on(v);
            }
        }
        break;
    }
    case R_KOFF:
    case R_KOFF + 2: {
        u32 bits = (u32)value << (reg == R_KOFF ? 0 : 16);
        for (int v = 0; v < VOICES; v++) {
            if (bits & (1u << v)) {
                key_off(v);
            }
        }
        break;
    }
    case R_ENDX:
    case R_ENDX + 2:
        break; /* read-only */
    case R_TRANSFER_ADDR:
        spu.transfer_addr = (u32)value * 8u;
        break;
    case R_FIFO: /* manual write transfer */
        spu.ram[spu.transfer_addr & (SPU_RAM_SIZE - 1)] = (u8)value;
        spu.ram[(spu.transfer_addr + 1) & (SPU_RAM_SIZE - 1)] = (u8)(value >> 8);
        spu.transfer_addr = (spu.transfer_addr + 2) & (SPU_RAM_SIZE - 1);
        break;
    default:
        if (reg < 0x180 && (reg & 0xF) == 0x6) {
            /* start address: takes effect on key on */
        } else if (reg < 0x180 && (reg & 0xF) == 0xE) {
            spu.voices[reg >> 4].repeat_addr = (u32)value * 8u;
        }
        break;
    }
}

void spu_dma_write(const u8 *src, u32 bytes) {
    for (u32 i = 0; i < bytes; i++) {
        spu.ram[(spu.transfer_addr + i) & (SPU_RAM_SIZE - 1)] = src[i];
    }
    spu.transfer_addr = (spu.transfer_addr + bytes) & (SPU_RAM_SIZE - 1);
}

void spu_dma_read(u8 *dst, u32 bytes) {
    for (u32 i = 0; i < bytes; i++) {
        dst[i] = spu.ram[(spu.transfer_addr + i) & (SPU_RAM_SIZE - 1)];
    }
    spu.transfer_addr = (spu.transfer_addr + bytes) & (SPU_RAM_SIZE - 1);
}

/* --- ADPCM -------------------------------------------------------------------------------- */

static s16 clamp16(s32 v) {
    return (s16)(v < -0x8000 ? -0x8000 : (v > 0x7FFF ? 0x7FFF : v));
}

static void decode_block(Voice *vc) {
    const u8 *blk = &spu.ram[vc->addr & (SPU_RAM_SIZE - 1) & ~15u];
    int shift = blk[0] & 0xF;
    int filter = (blk[0] >> 4) & 7;
    if (shift > 12) {
        shift = 9;
    }
    if (filter > 4) {
        filter = 4;
    }
    for (int i = 0; i < 28; i++) {
        int nib = (blk[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
        s32 s = (s32)((s16)(nib << 12)) >> shift;
        s += (vc->prev1 * adpcm_pos[filter] + vc->prev2 * adpcm_neg[filter] + 32) / 64;
        s16 out = clamp16(s);
        vc->prev2 = vc->prev1;
        vc->prev1 = out;
        vc->decoded[i] = out;
    }
}

/* Advances to the next block, honouring the loop flags of the block just finished. */
static void next_block(int v) {
    Voice *vc = &spu.voices[v];
    const u8 flags = spu.ram[(vc->addr + 1) & (SPU_RAM_SIZE - 1)];
    if (flags & 0x01) { /* loop end */
        spu.endx |= 1u << v;
        vc->addr = vc->repeat_addr;
        if (!(flags & 0x02)) { /* not repeating: the voice is muted */
            vc->phase = ENV_RELEASE;
            vc->level = 0;
        }
    } else {
        vc->addr = (vc->addr + 16) & (SPU_RAM_SIZE - 1);
    }
    const u8 next_flags = spu.ram[(vc->addr + 1) & (SPU_RAM_SIZE - 1)];
    if (next_flags & 0x04) { /* loop start */
        vc->repeat_addr = vc->addr;
    }
    decode_block(vc);
}

/* --- envelope ----------------------------------------------------------------------------- */

static void env_step(Voice *vc, bool exponential, bool decrease, int shift, int step) {
    if (vc->env_wait > 0) {
        vc->env_wait--;
        return;
    }
    int cycles = 1 << (shift - 11 > 0 ? shift - 11 : 0);
    s32 delta = (decrease ? (-8 + step) : (7 - step)) << (11 - shift > 0 ? 11 - shift : 0);
    if (exponential && !decrease && vc->level > 0x6000) {
        cycles *= 4;
    }
    if (exponential && decrease) {
        delta = delta * vc->level / 0x8000;
    }
    vc->env_wait = cycles - 1;
    vc->level += delta;
    vc->level = vc->level < 0 ? 0 : (vc->level > 0x7FFF ? 0x7FFF : vc->level);
}

static void envelope(int v) {
    Voice *vc = &spu.voices[v];
    u16 a1 = VREG(v, 0x8), a2 = VREG(v, 0xA);
    switch (vc->phase) {
    case ENV_ATTACK:
        env_step(vc, a1 >> 15, false, (a1 >> 10) & 0x1F, (a1 >> 8) & 3);
        if (vc->level >= 0x7FFF) {
            vc->phase = ENV_DECAY;
            vc->env_wait = 0;
        }
        break;
    case ENV_DECAY: {
        s32 sustain = ((a1 & 0xF) + 1) * 0x800;
        env_step(vc, true, true, ((a1 >> 4) & 0xF) << 2, 0);
        if (vc->level <= sustain) {
            vc->phase = ENV_SUSTAIN;
            vc->env_wait = 0;
        }
        break;
    }
    case ENV_SUSTAIN:
        env_step(vc, a2 >> 15, (a2 >> 14) & 1, (a2 >> 8) & 0x1F, (a2 >> 6) & 3);
        break;
    case ENV_RELEASE:
        env_step(vc, (a2 >> 5) & 1, true, (a2 & 0x1F) << 2, 0);
        if (vc->level <= 0) {
            vc->phase = ENV_OFF;
        }
        break;
    default:
        break;
    }
}

static void key_on(int v) {
    Voice *vc = &spu.voices[v];
    vc->addr = (u32)VREG(v, 0x6) * 8u;
    vc->counter = 0;
    vc->prev1 = vc->prev2 = 0;
    memset(vc->history, 0, sizeof vc->history);
    vc->phase = ENV_ATTACK;
    vc->level = 0;
    vc->env_wait = 0;
    spu.endx &= ~(1u << v);
    if (spu.ram[(vc->addr + 1) & (SPU_RAM_SIZE - 1)] & 0x04) {
        vc->repeat_addr = vc->addr;
    }
    decode_block(vc);
}

static void key_off(int v) {
    Voice *vc = &spu.voices[v];
    if (vc->phase != ENV_OFF) {
        vc->phase = ENV_RELEASE;
        vc->env_wait = 0;
    }
}

/* --- voice output ------------------------------------------------------------------------- */

static s16 voice_sample_at(const Voice *vc, int i) {
    return i < 0 ? vc->history[4 + i] : vc->decoded[i];
}

/* Cubic (Catmull-Rom) interpolation between samples i and i+1. */
static s32 interpolate(const Voice *vc, int i, u32 frac12) {
    s32 p0 = voice_sample_at(vc, i - 1), p1 = voice_sample_at(vc, i);
    s32 p2 = i + 1 < 28 ? vc->decoded[i + 1] : p1, p3 = i + 2 < 28 ? vc->decoded[i + 2] : p2;
    s64 t = frac12;
    s64 a = -p0 + 3 * p1 - 3 * p2 + p3, b = 2 * p0 - 5 * p1 + 4 * p2 - p3, c = -p0 + p2;
    s64 r = ((((a * t >> 12) + b) * t >> 12) + c) * t >> 12;
    return (s32)(p1 + r / 2);
}

static s32 volume(u16 reg) {
    if (!(reg & 0x8000)) {
        return (s16)(reg << 1); /* fixed volume, -0x8000..0x7FFE */
    }
    /* Sweep mode: approximated by its end point. */
    return (reg & 0x2000) ? 0 : 0x7FFF;
}

static void noise_tick(void) {
    u16 cnt = REG(R_SPUCNT);
    int shift = (cnt >> 10) & 0xF, step = ((cnt >> 8) & 3) + 4;
    spu.noise_wait -= step;
    if (spu.noise_wait < 0) {
        u32 l = spu.noise_lfsr;
        u32 parity = ((l >> 15) ^ (l >> 12) ^ (l >> 11) ^ (l >> 10) ^ 1) & 1;
        spu.noise_lfsr = (l << 1) | parity;
        spu.noise_level = (s16)spu.noise_lfsr;
        spu.noise_wait += 0x20000 >> shift;
    }
}

static void mix_voice(int v, s32 *l, s32 *r) {
    Voice *vc = &spu.voices[v];
    if (vc->phase == ENV_OFF) {
        vc->last_out = 0;
        return;
    }
    envelope(v);
    bool noise = (regs_pair(R_NON) >> v) & 1;
    s32 sample = noise ? spu.noise_level : interpolate(vc, vc->block_pos, vc->counter & 0xFFF);
    s32 out = sample * vc->level / 0x8000;
    vc->last_out = out;

    *l += out * volume(VREG(v, 0x0)) / 0x8000;
    *r += out * volume(VREG(v, 0x2)) / 0x8000;

    u32 step = VREG(v, 0x4);
    if (v > 0 && ((regs_pair(R_PMON) >> v) & 1)) {
        s32 factor = spu.voices[v - 1].last_out + 0x8000;
        step = (u32)((s64)(s16)step * factor >> 15) & 0xFFFF;
    }
    step = step > 0x3FFF ? 0x4000 : step;
    vc->counter += step;
    while (vc->counter >= 0x1000) {
        vc->counter -= 0x1000;
        if (++vc->block_pos >= 28) {
            memcpy(vc->history, &vc->decoded[24], sizeof vc->history);
            vc->block_pos = 0;
            next_block(v);
        }
    }
}

/* --- CD audio ----------------------------------------------------------------------------- */

static void cd_push(s16 l, s16 r) {
    u32 next = (spu.cd_write + 1) % CD_RING;
    if (next == spu.cd_read) {
        spu.cd_read = (spu.cd_read + 1) % CD_RING; /* overflow: drop the oldest */
    }
    spu.cd_ring[spu.cd_write][0] = l;
    spu.cd_ring[spu.cd_write][1] = r;
    spu.cd_write = next;
}

void spu_cdda_feed(const u8 *sector) {
    for (u32 i = 0; i < 2352; i += 4) {
        cd_push((s16)(sector[i] | sector[i + 1] << 8), (s16)(sector[i + 2] | sector[i + 3] << 8));
    }
}

static s16 xa_sample(int nibble_or_byte, int bits, int shift, int filter, s32 *prev) {
    s32 s = bits == 4 ? (s32)((s16)(nibble_or_byte << 12)) >> shift
                      : (s32)((s16)(nibble_or_byte << 8)) >> shift;
    s += (prev[0] * adpcm_pos[filter] + prev[1] * adpcm_neg[filter] + 32) / 64;
    s16 out = clamp16(s);
    prev[1] = prev[0];
    prev[0] = out;
    return out;
}

/* Pushes decoded XA samples, resampling 37.8/18.9 kHz to 44.1 kHz linearly. */
static void xa_output(const s16 *left, const s16 *right, int n, u32 rate) {
    u32 step = (u32)(((u64)rate << 16) / 44100u);
    for (int i = 0; i < n; i++) {
        while (spu.xa_phase < 0x10000) {
            u32 f = spu.xa_phase;
            s32 l = spu.xa_last[0] + (((s32)left[i] - spu.xa_last[0]) * (s32)f >> 16);
            s32 r = spu.xa_last[1] + (((s32)right[i] - spu.xa_last[1]) * (s32)f >> 16);
            cd_push((s16)l, (s16)r);
            spu.xa_phase += step;
        }
        spu.xa_phase -= 0x10000;
        spu.xa_last[0] = left[i];
        spu.xa_last[1] = right[i];
    }
}

void spu_xa_feed(const u8 *sector) {
    u8 coding = sector[19];
    bool stereo = (coding & 3) == 1;
    u32 rate = ((coding >> 2) & 3) == 1 ? 18900u : 37800u;
    int bits = ((coding >> 4) & 3) == 1 ? 8 : 4;
    const u8 *data = sector + 24;

    static s16 out_l[4032], out_r[4032];
    int n = 0;
    s16 mono[4032];
    int nm = 0;
    for (int g = 0; g < 18; g++) {
        const u8 *grp = data + g * 128;
        int units = bits == 4 ? 8 : 4;
        for (int u = 0; u < units; u++) {
            u8 hdr = grp[4 + u];
            int shift = (bits == 4 ? 12 : 8) - (hdr & 0xF);
            shift = shift < 0 ? 0 : shift;
            int filter = (hdr >> 4) & 3;
            int ch = stereo ? (u & 1) : 0;
            for (int j = 0; j < 28; j++) {
                int raw;
                if (bits == 4) {
                    u8 b = grp[16 + j * 4 + u / 2];
                    raw = (u & 1) ? b >> 4 : b & 0xF;
                } else {
                    raw = grp[16 + j * 4 + u];
                }
                s16 s = xa_sample(raw, bits, shift, filter, spu.xa_prev[ch]);
                if (!stereo) {
                    mono[nm++] = s;
                } else if (ch == 0) {
                    out_l[n + j] = s;
                } else {
                    out_r[n + j] = s;
                }
            }
            if (stereo && ch == 1) {
                n += 28;
            }
        }
    }
    if (stereo) {
        xa_output(out_l, out_r, n, rate);
    } else {
        xa_output(mono, mono, nm, rate);
    }
}

/* --- mixing ------------------------------------------------------------------------------- */

void spu_render(s16 *out, int frames) {
    for (int f = 0; f < frames; f++) {
        noise_tick();
        s32 l = 0, r = 0;
        for (int v = 0; v < VOICES; v++) {
            mix_voice(v, &l, &r);
        }
        l = l * volume(REG(R_MAIN_VOL_L)) / 0x8000 * gain_sfx / 256;
        r = r * volume(REG(R_MAIN_VOL_R)) / 0x8000 * gain_sfx / 256;

        if (spu.cd_read != spu.cd_write && (REG(R_SPUCNT) & 0x01)) { /* CD audio enabled */
            l +=
                (s32)spu.cd_ring[spu.cd_read][0] * (s16)REG(R_CD_VOL_L) / 0x8000 * gain_music / 256;
            r +=
                (s32)spu.cd_ring[spu.cd_read][1] * (s16)REG(R_CD_VOL_R) / 0x8000 * gain_music / 256;
        }
        if (spu.cd_read != spu.cd_write) {
            spu.cd_read = (spu.cd_read + 1) % CD_RING;
        }

        if (!(REG(R_SPUCNT) & 0x8000)) { /* SPU disabled */
            l = r = 0;
        }
        out[f * 2] = clamp16(l * gain_master / 256);
        out[f * 2 + 1] = clamp16(r * gain_master / 256);
    }
}

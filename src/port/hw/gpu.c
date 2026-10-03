/* Software GPU: GP0/GP1 command processing and a reference rasterizer for polygons, lines and
 * rectangles (flat/gouraud, textured 4/8/15-bit, semi-transparency, dithering, mask bit).
 *
 * Upscaling: VRAM is stored at `scale` x the native 1024x512. Polygons and lines are rasterized
 * at the internal resolution (smooth edges); textures, palettes, sprites and fills address VRAM
 * at native 1x positions, so artwork stays pixel-exact. CPU uploads replicate each pixel into a
 * scale x scale block, and readbacks / 24-bit (movie) display sample the block's top-left pixel.
 * Reference: psx-spx "GPU". */
#include "port/hw/gpu.h"
#include "port/savestate.h"

#include "port/hw/hw.h"
#include "port/hw/widescreen.h"
#include "port/runtime.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VRAM_W 1024
#define VRAM_H 512
#define MAX_SCALE 8

typedef struct GpuState {
    u16 *vram; /* (VRAM_W * scale) x (VRAM_H * scale) */
    int scale;

    /* GP0 command assembly */
    u32 fifo[16];
    u32 fifo_len, fifo_need;
    bool polyline;
    bool polyline_have_color;
    u32 polyline_color;

    /* CPU<->VRAM transfers */
    bool uploading, downloading;
    u32 xfer_x, xfer_y, xfer_w, xfer_h, xfer_i;

    /* drawing environment (native coordinates) */
    u32 texpage;          /* E1 bits 0-13 */
    u32 tex_window;       /* E2 */
    int clip_x1, clip_y1; /* E3 */
    int clip_x2, clip_y2; /* E4 */
    int off_x, off_y;     /* E5 */
    bool mask_set, mask_check;
    bool tex_flip_x, tex_flip_y;

    /* display */
    u32 disp_x, disp_y;
    u32 h_start, h_end, v_start, v_end;
    u32 disp_mode; /* GP1(08) bits */
    bool disp_off;
    u32 dma_dir;
    bool odd_line;
    u32 frame;
    u32 flips; /* display start changes = game frames presented */
    /* Widescreen: does the presented frame contain 3D? Still pictures and movies do not. */
    u32 polys3d_since_flip;
    u32 vblanks_since_flip;
    bool frame_has_3d;
} GpuState;

static GpuState g = {.disp_off = true};

/* --- VRAM access -------------------------------------------------------------------------- */

static int sext11(u32 v) {
    return (int)((v & 0x7FFu) ^ 0x400u) - 0x400;
}

static void ensure_vram(void) {
    if (g.vram == NULL) {
        g.scale = 1;
        g.vram = calloc((size_t)VRAM_W * VRAM_H, sizeof(u16));
    }
}

/* Pixel at internal (scaled) coordinates. */
static u16 *hpx(int x, int y) {
    int w = VRAM_W * g.scale, h = VRAM_H * g.scale;
    x %= w;
    y %= h;
    x += x < 0 ? w : 0;
    y += y < 0 ? h : 0;
    return &g.vram[(size_t)y * (size_t)w + (size_t)x];
}

/* Pixel at native coordinates (top-left of its scale x scale block). */
static u16 *px(int x, int y) {
    return hpx((x & (VRAM_W - 1)) * g.scale, (y & (VRAM_H - 1)) * g.scale);
}

/* Writes a native pixel: fills its whole block. */
static void put_native(int x, int y, u16 v) {
    int s = g.scale;
    int bx = (x & (VRAM_W - 1)) * s, by = (y & (VRAM_H - 1)) * s;
    for (int j = 0; j < s; j++) {
        u16 *row = hpx(bx, by + j);
        for (int i = 0; i < s; i++) {
            row[i] = v;
        }
    }
}

void gpu_set_scale(int scale) {
    scale = scale < 1 ? 1 : (scale > MAX_SCALE ? MAX_SCALE : scale);
    ensure_vram();
    if (scale == g.scale) {
        return;
    }
    /* Resample the current contents: take each native pixel and replicate it. */
    u16 *native = malloc((size_t)VRAM_W * VRAM_H * sizeof(u16));
    for (int y = 0; y < VRAM_H; y++) {
        for (int x = 0; x < VRAM_W; x++) {
            native[y * VRAM_W + x] = *px(x, y);
        }
    }
    free(g.vram);
    g.scale = scale;
    g.vram = malloc((size_t)VRAM_W * VRAM_H * (size_t)(scale * scale) * sizeof(u16));
    for (int y = 0; y < VRAM_H; y++) {
        for (int x = 0; x < VRAM_W; x++) {
            put_native(x, y, native[y * VRAM_W + x]);
        }
    }
    free(native);
    NC_LOG("gpu: internal resolution %dx", scale);
}

int gpu_scale(void) {
    ensure_vram();
    return g.scale;
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static const int dither[4][4] = {
    {-4, 0, -3, 1},
    {2, -2, 3, -1},
    {-3, 1, -4, 0},
    {3, -1, 2, -2},
};

typedef struct Vertex {
    int x, y; /* native screen coordinates (offset applied) */
    int r, g, b;
    int u, v;
} Vertex;

struct TexEntry;

typedef struct DrawState {
    bool textured, raw, semi, gouraud, dither;
    u32 clut_x, clut_y;
    u32 tp_x, tp_y, tp_depth, blend;
    const struct TexEntry *hires; /* xBRZ-upscaled texture page, or NULL */
} DrawState;

static void apply_texpage(DrawState *ds, u32 tp) {
    ds->tp_x = (tp & 0xF) * 64;
    ds->tp_y = ((tp >> 4) & 1) * 256;
    ds->blend = (tp >> 5) & 3;
    ds->tp_depth = (tp >> 7) & 3;
}

static u16 sample(const DrawState *ds, int u, int v) {
    u32 tw = g.tex_window;
    u32 mask_x = (tw & 0x1F) * 8, mask_y = ((tw >> 5) & 0x1F) * 8;
    u32 off_x = ((tw >> 10) & 0x1F) * 8, off_y = ((tw >> 15) & 0x1F) * 8;
    u32 uu = ((u32)u & 0xFF), vv = ((u32)v & 0xFF);
    uu = (uu & ~mask_x) | (off_x & mask_x);
    vv = (vv & ~mask_y) | (off_y & mask_y);

    switch (ds->tp_depth) {
    case 0: { /* 4-bit CLUT */
        u16 texel = *px((int)(ds->tp_x + uu / 4), (int)(ds->tp_y + vv));
        u32 idx = (texel >> ((uu & 3) * 4)) & 0xF;
        return *px((int)(ds->clut_x + idx), (int)ds->clut_y);
    }
    case 1: { /* 8-bit CLUT */
        u16 texel = *px((int)(ds->tp_x + uu / 2), (int)(ds->tp_y + vv));
        u32 idx = (texel >> ((uu & 1) * 8)) & 0xFF;
        return *px((int)(ds->clut_x + idx), (int)ds->clut_y);
    }
    default: /* 15-bit direct */
        return *px((int)(ds->tp_x + uu), (int)(ds->tp_y + vv));
    }
}

/* Clip rectangle in internal coordinates. */
static bool in_clip(int hx, int hy) {
    int s = g.scale;
    return hx >= g.clip_x1 * s && hx < (g.clip_x2 + 1) * s && hy >= g.clip_y1 * s &&
           hy < (g.clip_y2 + 1) * s;
}

/* Shades and writes one internal-resolution pixel. Dithering follows native pixel positions.
 * For textured pixels, (tr, tg, tb) is the texel colour in 8 bits per channel and `tex_flag` its
 * semi-transparency / mask bit. */
static void plot_px(const DrawState *ds, int hx, int hy, int r, int gg, int b, bool textured_px,
                    int tr, int tg, int tb, bool tex_flag) {
    if (!in_clip(hx, hy)) {
        return;
    }
    u16 *dst = hpx(hx, hy);
    if (g.mask_check && (*dst & 0x8000)) {
        return;
    }
    bool semi = ds->semi;
    if (textured_px) {
        if (ds->raw) {
            r = tr;
            gg = tg;
            b = tb;
        } else {
            r = tr * r / 128;
            gg = tg * gg / 128;
            b = tb * b / 128;
        }
        semi = semi && tex_flag;
    }
    if (ds->dither) {
        int d = dither[(hy / g.scale) & 3][(hx / g.scale) & 3];
        r += d;
        gg += d;
        b += d;
    }
    int r5 = clamp(r, 0, 255) >> 3, g5 = clamp(gg, 0, 255) >> 3, b5 = clamp(b, 0, 255) >> 3;

    if (semi) {
        int br = *dst & 0x1F, bg = (*dst >> 5) & 0x1F, bb = (*dst >> 10) & 0x1F;
        switch (ds->blend) {
        case 0:
            r5 = (br + r5) / 2;
            g5 = (bg + g5) / 2;
            b5 = (bb + b5) / 2;
            break;
        case 1:
            r5 = br + r5;
            g5 = bg + g5;
            b5 = bb + b5;
            break;
        case 2:
            r5 = br - r5;
            g5 = bg - g5;
            b5 = bb - b5;
            break;
        default:
            r5 = br + r5 / 4;
            g5 = bg + g5 / 4;
            b5 = bb + b5 / 4;
            break;
        }
        r5 = clamp(r5, 0, 31);
        g5 = clamp(g5, 0, 31);
        b5 = clamp(b5, 0, 31);
    }
    u16 mask = (g.mask_set || (textured_px && tex_flag)) ? 0x8000 : 0;
    *dst = (u16)(r5 | (g5 << 5) | (b5 << 10) | mask);
}

/* Same, with a native 15-bit texel (0 = fully transparent). */
static void plot(const DrawState *ds, int hx, int hy, int r, int gg, int b, bool textured_px,
                 u16 texel) {
    if (textured_px && texel == 0) {
        return;
    }
    plot_px(ds, hx, hy, r, gg, b, textured_px, (texel & 0x1F) << 3, ((texel >> 5) & 0x1F) << 3,
            ((texel >> 10) & 0x1F) << 3, (texel & 0x8000) != 0);
}

/* --- texture upscaling (xBRZ) ---------------------------------------------------------------
 * Each texture page + palette the game draws with is decoded to ARGB, scaled with xBRZ and
 * cached. VRAM is tracked in 64x256 regions: any write to a region (upload, fill, copy, drawing)
 * bumps its generation, and cached textures built from it are rebuilt on next use. Textures that
 * keep changing (render-to-texture) are drawn without upscaling for a while. */

void xbrz_upscale_argb(int factor, const u32 *src, u32 *dst, int width, int height);

#define REGION_W 64
#define REGION_H 256
#define REGIONS_X (VRAM_W / REGION_W)
#define REGIONS (REGIONS_X * (VRAM_H / REGION_H))
#define TEX_BUDGET_BYTES (256u << 20)
#define TEX_MAX_ENTRIES 128
#define TEX_MAX_REGIONS 6
#define VOLATILE_REBUILDS 3 /* rebuilds within VOLATILE_WINDOW frames: stop upscaling it */
#define VOLATILE_WINDOW 60
#define VOLATILE_COOLDOWN 600

typedef struct TexEntry {
    bool used;
    u32 key;
    int factor;
    u8 nregions;
    u8 regions[TEX_MAX_REGIONS];
    u32 gens[TEX_MAX_REGIONS];
    u32 *pixels; /* (256 * factor)^2 ARGB */
    u8 flags[256 * 256]; /* semi-transparency bit of each original texel */
    u32 last_use;
    u32 window_start; /* volatile detection */
    u32 rebuilds;
    u32 volatile_until;
} TexEntry;

static u32 region_gen[REGIONS];
static TexEntry tex_cache[TEX_MAX_ENTRIES];
static int tex_factor = 1; /* 1 = off */
static u32 tex_clock;

/* Marks native VRAM rectangle [x, x+w) x [y, y+h) as written (wraps like VRAM addressing). */
static void mark_written(int x, int y, int w, int h) {
    if (tex_factor <= 1 || w <= 0 || h <= 0) {
        return;
    }
    if (w > VRAM_W) {
        w = VRAM_W;
    }
    if (h > VRAM_H) {
        h = VRAM_H;
    }
    int rx0 = (x & (VRAM_W - 1)) / REGION_W, ry0 = (y & (VRAM_H - 1)) / REGION_H;
    int rx1 = ((x + w - 1) & (VRAM_W - 1)) / REGION_W,
        ry1 = ((y + h - 1) & (VRAM_H - 1)) / REGION_H;
    for (int ry = ry0;; ry = (ry + 1) % (VRAM_H / REGION_H)) {
        for (int rx = rx0;; rx = (rx + 1) % REGIONS_X) {
            region_gen[ry * REGIONS_X + rx]++;
            if (rx == rx1) {
                break;
            }
        }
        if (ry == ry1) {
            break;
        }
    }
}

static void tex_clear(void) {
    for (int i = 0; i < TEX_MAX_ENTRIES; i++) {
        free(tex_cache[i].pixels);
    }
    memset(tex_cache, 0, sizeof tex_cache);
}

void gpu_set_texture_scale(int factor) {
    factor = factor < 1 ? 1 : (factor > 4 ? 4 : factor);
    if (factor != tex_factor) {
        tex_clear();
        tex_factor = factor;
    }
}

/* Regions holding a texture page (and its palette). */
static int tex_regions(const DrawState *ds, u8 out[TEX_MAX_REGIONS]) {
    int n = 0;
    int width = ds->tp_depth == 0 ? 64 : (ds->tp_depth == 1 ? 128 : 256);
    int ry = (int)ds->tp_y / REGION_H;
    for (int x = (int)ds->tp_x; x < (int)ds->tp_x + width; x += REGION_W) {
        out[n++] = (u8)(ry * REGIONS_X + ((x & (VRAM_W - 1)) / REGION_W));
    }
    if (ds->tp_depth < 2) {
        int entries = ds->tp_depth == 0 ? 16 : 256;
        int cy = (int)(ds->clut_y & (VRAM_H - 1)) / REGION_H;
        for (int x = (int)ds->clut_x; x < (int)ds->clut_x + entries; x += REGION_W) {
            u8 r = (u8)(cy * REGIONS_X + ((x & (VRAM_W - 1)) / REGION_W));
            bool dup = false;
            for (int i = 0; i < n; i++) {
                dup = dup || out[i] == r;
            }
            if (!dup && n < TEX_MAX_REGIONS) {
                out[n++] = r;
            }
        }
    }
    return n;
}

static void tex_build(TexEntry *e, const DrawState *ds) {
    static u32 decoded[256 * 256];
    DrawState plain = *ds;
    u32 saved_window = g.tex_window;
    g.tex_window = 0; /* decode the whole page; the window applies when sampling */
    for (int v = 0; v < 256; v++) {
        for (int u = 0; u < 256; u++) {
            u16 t = sample(&plain, u, v);
            e->flags[v * 256 + u] = (t & 0x8000) != 0;
            u32 r = (t & 0x1F) << 3, gg = ((t >> 5) & 0x1F) << 3, b = ((t >> 10) & 0x1F) << 3;
            decoded[v * 256 + u] = t == 0 ? 0 : (0xFF000000u | r << 16 | gg << 8 | b);
        }
    }
    g.tex_window = saved_window;
    size_t side = 256u * (size_t)tex_factor;
    if (e->pixels == NULL || e->factor != tex_factor) {
        free(e->pixels);
        e->pixels = malloc(side * side * sizeof(u32));
        e->factor = tex_factor;
    }
    if (e->pixels != NULL) {
        xbrz_upscale_argb(tex_factor, decoded, e->pixels, 256, 256);
    }
}

/* Returns the upscaled texture for a primitive, or NULL to sample natively. */
static const TexEntry *tex_lookup(const DrawState *ds) {
    if (tex_factor <= 1 || g.scale <= 1 || !ds->textured) {
        return NULL;
    }
    u32 key = (ds->tp_x / 64) | ((ds->tp_y / 256) << 4) | (ds->tp_depth << 5);
    if (ds->tp_depth < 2) {
        key |= ((ds->clut_x / 16) << 7) | ((ds->clut_y & 0x1FF) << 13);
    }
    tex_clock++;
    TexEntry *hit = NULL, *victim = NULL;
    int max_entries = (int)(TEX_BUDGET_BYTES / (256u * 256u * 4u * (u32)(tex_factor * tex_factor)));
    max_entries = max_entries > TEX_MAX_ENTRIES ? TEX_MAX_ENTRIES : max_entries;
    for (int i = 0; i < max_entries; i++) {
        TexEntry *e = &tex_cache[i];
        if (e->used && e->key == key) {
            hit = e;
            break;
        }
        if (victim == NULL || !e->used || (victim->used && e->last_use < victim->last_use)) {
            victim = e;
        }
    }
    TexEntry *e = hit;
    bool stale = false;
    if (e == NULL) {
        e = victim;
        e->used = true;
        e->key = key;
        e->rebuilds = 0;
        e->window_start = g.frame;
        e->volatile_until = 0;
        stale = true;
    } else {
        for (int i = 0; i < e->nregions; i++) {
            stale = stale || region_gen[e->regions[i]] != e->gens[i];
        }
    }
    e->last_use = tex_clock;
    if (g.frame < e->volatile_until) {
        return NULL;
    }
    if (stale) {
        if (hit != NULL) {
            if (g.frame - e->window_start > VOLATILE_WINDOW) {
                e->window_start = g.frame;
                e->rebuilds = 0;
            }
            if (++e->rebuilds > VOLATILE_REBUILDS) {
                e->volatile_until = g.frame + VOLATILE_COOLDOWN;
                return NULL;
            }
        }
        e->nregions = (u8)tex_regions(ds, e->regions);
        for (int i = 0; i < e->nregions; i++) {
            e->gens[i] = region_gen[e->regions[i]];
        }
        tex_build(e, ds);
    }
    return e->pixels != NULL ? e : NULL;
}

/* Samples the upscaled texture at texel coordinates (uf, vf); returns false if transparent. */
static bool sample_hires(const DrawState *ds, double uf, double vf, int *r, int *gg, int *b,
                         bool *flag) {
    const TexEntry *e = ds->hires;
    int f = e->factor;
    double fu = floor(uf), fv = floor(vf);
    int ui = (int)fu, vi = (int)fv;
    int su = (int)((uf - fu) * f), sv = (int)((vf - fv) * f);
    su = su >= f ? f - 1 : su;
    sv = sv >= f ? f - 1 : sv;
    u32 tw = g.tex_window;
    u32 mask_x = (tw & 0x1F) * 8, mask_y = ((tw >> 5) & 0x1F) * 8;
    u32 off_x = ((tw >> 10) & 0x1F) * 8, off_y = ((tw >> 15) & 0x1F) * 8;
    u32 uu = ((u32)ui & 0xFF), vv = ((u32)vi & 0xFF);
    uu = (uu & ~mask_x) | (off_x & mask_x);
    vv = (vv & ~mask_y) | (off_y & mask_y);
    u32 p = e->pixels[(vv * (u32)f + (u32)sv) * 256u * (u32)f + uu * (u32)f + (u32)su];
    if ((p >> 24) < 128) {
        return false;
    }
    *r = (int)((p >> 16) & 0xFF);
    *gg = (int)((p >> 8) & 0xFF);
    *b = (int)(p & 0xFF);
    /* Semi-transparency follows the original texel. */
    *flag = e->flags[vv * 256 + uu] != 0;
    return true;
}

static void plot_hires(const DrawState *ds, int hx, int hy, int r, int gg, int b, double uf,
                       double vf) {
    int tr, tg, tb;
    bool flag;
    if (sample_hires(ds, uf, vf, &tr, &tg, &tb, &flag)) {
        plot_px(ds, hx, hy, r, gg, b, true, tr, tg, tb, flag);
    }
}

/* --- triangles ------------------------------------------------------------------------ */

/* Attribute plane: value = a*x + b*y + c over internal pixel centres. */
typedef struct Plane {
    double a, b, c;
} Plane;

static Plane plane(const double x[3], const double y[3], const double v[3], double det) {
    Plane p;
    p.a = ((v[1] - v[0]) * (y[2] - y[0]) - (v[2] - v[0]) * (y[1] - y[0])) / det;
    p.b = ((v[2] - v[0]) * (x[1] - x[0]) - (v[1] - v[0]) * (x[2] - x[0])) / det;
    p.c = v[0] - p.a * x[0] - p.b * y[0];
    return p;
}

static s64 edge(s64 ax, s64 ay, s64 bx, s64 by, s64 x, s64 y) {
    return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}

/* Top-left fill rule: pixels exactly on an edge belong to it only for top/left edges. */
static bool is_top_left(s64 ax, s64 ay, s64 bx, s64 by) {
    return (ay == by && bx < ax) || (by < ay);
}

static void draw_triangle(const DrawState *ds, Vertex v0, Vertex v1, Vertex v2) {
    /* Hardware rejects polygons larger than 1023x511 (native). */
    int minx = v0.x, maxx = v0.x, miny = v0.y, maxy = v0.y;
    const Vertex *vs[3] = {&v0, &v1, &v2};
    for (int i = 1; i < 3; i++) {
        minx = vs[i]->x < minx ? vs[i]->x : minx;
        maxx = vs[i]->x > maxx ? vs[i]->x : maxx;
        miny = vs[i]->y < miny ? vs[i]->y : miny;
        maxy = vs[i]->y > maxy ? vs[i]->y : maxy;
    }
    if (maxx - minx >= 1024 || maxy - miny >= 512) {
        return;
    }
    mark_written(minx, miny, maxx - minx + 1, maxy - miny + 1);

    /* Work in internal coordinates; vertices sit on native pixel corners. */
    const s64 s = g.scale;
    s64 x0 = v0.x * s, y0 = v0.y * s, x1 = v1.x * s, y1 = v1.y * s, x2 = v2.x * s, y2 = v2.y * s;
    s64 area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;
    }
    if (area < 0) {
        s64 tx = x1, ty = y1;
        x1 = x2;
        y1 = y2;
        x2 = tx;
        y2 = ty;
        Vertex t = v1;
        v1 = v2;
        v2 = t;
        area = -area;
    }

    int hminx = (int)(minx * s), hmaxx = (int)((maxx + 1) * s - 1);
    int hminy = (int)(miny * s), hmaxy = (int)((maxy + 1) * s - 1);
    hminx = hminx < g.clip_x1 * (int)s ? g.clip_x1 * (int)s : hminx;
    hminy = hminy < g.clip_y1 * (int)s ? g.clip_y1 * (int)s : hminy;
    hmaxx = hmaxx > (g.clip_x2 + 1) * (int)s - 1 ? (g.clip_x2 + 1) * (int)s - 1 : hmaxx;
    hmaxy = hmaxy > (g.clip_y2 + 1) * (int)s - 1 ? (g.clip_y2 + 1) * (int)s - 1 : hmaxy;
    if (hminx > hmaxx || hminy > hmaxy) {
        return;
    }

    int bias0 = is_top_left(x1, y1, x2, y2) ? 0 : -1;
    int bias1 = is_top_left(x2, y2, x0, y0) ? 0 : -1;
    int bias2 = is_top_left(x0, y0, x1, y1) ? 0 : -1;

    /* Attribute planes (sampled at pixel positions, like the hardware at 1x). */
    double px_[3] = {(double)x0, (double)x1, (double)x2},
           py_[3] = {(double)y0, (double)y1, (double)y2};
    double det = (double)area;
    double vr[3] = {v0.r, v1.r, v2.r}, vg[3] = {v0.g, v1.g, v2.g}, vb[3] = {v0.b, v1.b, v2.b};
    double vu[3] = {v0.u, v1.u, v2.u}, vv[3] = {v0.v, v1.v, v2.v};
    Plane pr = plane(px_, py_, vr, det), pg = plane(px_, py_, vg, det);
    Plane pb = plane(px_, py_, vb, det);
    Plane pu = plane(px_, py_, vu, det), pv = plane(px_, py_, vv, det);
    /* At higher scales, sample attributes at the pixel centre in native units. */
    double centre = s > 1 ? 0.5 : 0.0;

    for (int y = hminy; y <= hmaxy; y++) {
        s64 w0 = edge(x1, y1, x2, y2, hminx, y), w1 = edge(x2, y2, x0, y0, hminx, y);
        s64 w2 = edge(x0, y0, x1, y1, hminx, y);
        s64 d0 = -(y2 - y1), d1 = -(y0 - y2), d2 = -(y1 - y0);
        double fy = (double)y + centre;
        for (int x = hminx; x <= hmaxx; x++, w0 += d0, w1 += d1, w2 += d2) {
            if (w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) {
                continue;
            }
            double fx = (double)x + centre;
            int r = v0.r, gg = v0.g, b = v0.b;
            if (ds->gouraud) {
                r = (int)(pr.a * fx + pr.b * fy + pr.c);
                gg = (int)(pg.a * fx + pg.b * fy + pg.c);
                b = (int)(pb.a * fx + pb.b * fy + pb.c);
            }
            if (ds->hires != NULL) {
                plot_hires(ds, x, y, r, gg, b, pu.a * fx + pu.b * fy + pu.c,
                           pv.a * fx + pv.b * fy + pv.c);
            } else if (ds->textured) {
                int u = (int)(pu.a * fx + pu.b * fy + pu.c);
                int v = (int)(pv.a * fx + pv.b * fy + pv.c);
                plot(ds, x, y, r, gg, b, true, sample(ds, u, v));
            } else {
                plot(ds, x, y, r, gg, b, false, 0);
            }
        }
    }
}

/* 2D primitives span at most this much of the 320-wide screen before they are treated as
 * full-screen (fades, backdrops) and left stretched across the whole 16:9 frame. */
#define WS_FULLSCREEN_WIDTH 300

/* Debugging aid: NC_WS_TINT=1 draws primitives classified as 2D in red. */
static bool ws_tint(void) {
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("NC_WS_TINT") != NULL;
    }
    return enabled;
}

/* Widescreen: squeezes a 2D polygon (raw packet X, before the drawing offset). Returns true if
 * it was treated as 2D. */
static bool ws_fix_polygon(int *raw_x, const int *raw_y, int n) {
    if (!ws_enabled()) {
        return false;
    }
    int minx = raw_x[0], maxx = raw_x[0];
    bool projected = true;
    for (int i = 0; i < n; i++) {
        projected = projected && ws_is_projected(raw_x[i], raw_y[i]);
        minx = raw_x[i] < minx ? raw_x[i] : minx;
        maxx = raw_x[i] > maxx ? raw_x[i] : maxx;
    }
    if (projected || maxx - minx >= WS_FULLSCREEN_WIDTH) {
        return false;
    }
    int miny = raw_y[0], maxy = raw_y[0];
    for (int i = 1; i < n; i++) {
        miny = raw_y[i] < miny ? raw_y[i] : miny;
        maxy = raw_y[i] > maxy ? raw_y[i] : maxy;
    }
    ws_record_2d(minx, miny, maxx, maxy);
    int offset = ws_anchor_offset(minx, miny, maxx, maxy);
    for (int i = 0; i < n; i++) {
        raw_x[i] = ws_squeeze_x(raw_x[i], offset);
    }
    return true;
}

static void gp0_polygon(void) {
    u32 op = g.fifo[0] >> 24;
    bool quad = op & 0x08, textured = op & 0x04, gouraud = op & 0x10;
    DrawState ds = {.textured = textured,
                    .raw = op & 0x01,
                    .semi = op & 0x02,
                    .gouraud = gouraud,
                    .dither = (gouraud || (textured && !(op & 0x01))) && (g.texpage & 0x200)};
    apply_texpage(&ds, g.texpage);

    /* Packet: colour+cmd, xy, [uv], then per further vertex: [colour if gouraud], xy, [uv]. */
    Vertex v[4];
    int raw_x[4], raw_y[4];
    u32 i = 0;
    u32 color = 0;
    int nverts = quad ? 4 : 3;
    for (int n = 0; n < nverts; n++) {
        if (n == 0 || gouraud) {
            color = g.fifo[i++];
        }
        u32 xy = g.fifo[i++];
        raw_x[n] = sext11(xy);
        raw_y[n] = sext11(xy >> 16);
        v[n].y = raw_y[n] + g.off_y;
        v[n].r = (int)(color & 0xFF);
        v[n].g = (int)((color >> 8) & 0xFF);
        v[n].b = (int)((color >> 16) & 0xFF);
        v[n].u = v[n].v = 0;
        if (textured) {
            u32 uv = g.fifo[i++];
            v[n].u = (int)(uv & 0xFF);
            v[n].v = (int)((uv >> 8) & 0xFF);
            if (n == 0) {
                ds.clut_x = ((uv >> 16) & 0x3F) * 16;
                ds.clut_y = (uv >> 22) & 0x1FF;
            } else if (n == 1) {
                u32 tp = uv >> 16;
                apply_texpage(&ds, tp);
                g.texpage = (g.texpage & ~0x1FFu) | (tp & 0x1FF);
            }
        }
    }
    bool flat2d = ws_fix_polygon(raw_x, raw_y, nverts);
    if (ws_enabled() && !flat2d) {
        g.polys3d_since_flip++;
    }
    for (int n = 0; n < nverts; n++) {
        v[n].x = raw_x[n] + g.off_x;
        if (flat2d && ws_tint()) {
            v[n].r = 255;
            v[n].g = v[n].b = 0;
        }
    }
    ds.hires = tex_lookup(&ds);
    draw_triangle(&ds, v[0], v[1], v[2]);
    if (quad) {
        draw_triangle(&ds, v[1], v[2], v[3]);
    }
}

/* --- lines and rectangles ---------------------------------------------------------------- */

static int iabs(int v) {
    return v < 0 ? -v : v;
}

/* Lines are stepped at the internal resolution so they stay thin when upscaled. */
static void draw_line(const DrawState *ds, Vertex a, Vertex b) {
    int dx = b.x - a.x, dy = b.y - a.y;
    if (iabs(dx) >= 1024 || iabs(dy) >= 512) {
        return;
    }
    mark_written(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, iabs(dx) + 1, iabs(dy) + 1);
    int s = g.scale, half = s / 2;
    int hdx = dx * s, hdy = dy * s;
    int steps = iabs(hdx) > iabs(hdy) ? iabs(hdx) : iabs(hdy);
    for (int i = 0; i <= steps; i++) {
        int x = a.x * s + half + (steps ? hdx * i / steps : 0);
        int y = a.y * s + half + (steps ? hdy * i / steps : 0);
        int r = steps ? a.r + (b.r - a.r) * i / steps : a.r;
        int gg = steps ? a.g + (b.g - a.g) * i / steps : a.g;
        int bb = steps ? a.b + (b.b - a.b) * i / steps : a.b;
        plot(ds, x, y, r, gg, bb, false, 0);
    }
}

static Vertex line_vertex(u32 color, u32 xy) {
    Vertex v = {0};
    v.x = sext11(xy) + g.off_x;
    v.y = sext11(xy >> 16) + g.off_y;
    v.r = (int)(color & 0xFF);
    v.g = (int)((color >> 8) & 0xFF);
    v.b = (int)((color >> 16) & 0xFF);
    return v;
}

static void gp0_line(void) {
    u32 op = g.fifo[0] >> 24;
    bool gouraud = op & 0x10;
    DrawState ds = {
        .semi = op & 0x02, .gouraud = gouraud, .dither = gouraud && (g.texpage & 0x200)};
    apply_texpage(&ds, g.texpage);
    Vertex a, b;
    if (gouraud) {
        a = line_vertex(g.fifo[0], g.fifo[1]);
        b = line_vertex(g.fifo[2], g.fifo[3]);
    } else {
        a = line_vertex(g.fifo[0], g.fifo[1]);
        b = line_vertex(g.fifo[0], g.fifo[2]);
    }
    draw_line(&ds, a, b);
}

/* Rectangles (sprites) are drawn per native pixel, filling each internal block. */
static void gp0_rect(void) {
    u32 op = g.fifo[0] >> 24;
    bool textured = op & 0x04;
    DrawState ds = {.textured = textured, .raw = op & 0x01, .semi = op & 0x02};
    apply_texpage(&ds, g.texpage);
    u32 i = 1;
    u32 xy = g.fifo[i++];
    int raw_x0 = sext11(xy), raw_y0 = sext11(xy >> 16);
    int x0 = raw_x0 + g.off_x, y0 = raw_y0 + g.off_y;
    int u0 = 0, v0 = 0;
    if (textured) {
        u32 uv = g.fifo[i++];
        u0 = (int)(uv & 0xFF);
        v0 = (int)((uv >> 8) & 0xFF);
        ds.clut_x = ((uv >> 16) & 0x3F) * 16;
        ds.clut_y = (uv >> 22) & 0x1FF;
    }
    int w, h;
    switch ((op >> 3) & 3) {
    case 0: {
        u32 wh = g.fifo[i++];
        w = (int)(wh & 0x3FF);
        h = (int)((wh >> 16) & 0x1FF);
        break;
    }
    case 1:
        w = h = 1;
        break;
    case 2:
        w = h = 8;
        break;
    default:
        w = h = 16;
        break;
    }
    int r = (int)(g.fifo[0] & 0xFF), gg = (int)((g.fifo[0] >> 8) & 0xFF),
        b = (int)((g.fifo[0] >> 16) & 0xFF);
    /* Widescreen: sprites are drawn 3/4 as wide. Ones anchored on a projected point
     * (billboards) keep their position; HUD sprites are laid out by widescreen.c. */
    int dw = w;
    if (ws_enabled() && w < WS_FULLSCREEN_WIDTH) {
        if (!ws_is_projected(raw_x0, raw_y0)) {
            ws_record_2d(raw_x0, raw_y0, raw_x0 + w - 1, raw_y0 + h - 1);
            int offset = ws_anchor_offset(raw_x0, raw_y0, raw_x0 + w - 1, raw_y0 + h - 1);
            x0 = ws_squeeze_x(raw_x0, offset) + g.off_x;
            if (ws_tint()) {
                r = 255;
                gg = b = 0;
            }
        }
        dw = (w * 3 + 2) / 4;
    }
    int s = g.scale;
    mark_written(x0, y0, dw, h);
    ds.hires = tex_lookup(&ds);
    if (ds.hires != NULL) {
        /* Upscaled texture: sample every internal pixel at sub-texel precision. */
        for (int y = 0; y < h * s; y++) {
            double ty = (y + 0.5) / s;
            double vf = g.tex_flip_y ? v0 + 1 - ty : v0 + ty;
            for (int dx = 0; dx < dw * s; dx++) {
                double tx = (double)dx * w / dw / s + 0.5 / s;
                double uf = g.tex_flip_x ? u0 + 1 - tx : u0 + tx;
                plot_hires(&ds, x0 * s + dx, y0 * s + y, r, gg, b, uf, vf);
            }
        }
        return;
    }
    for (int y = 0; y < h; y++) {
        for (int dx = 0; dx < dw; dx++) {
            int x = dw == w ? dx : dx * w / dw;
            u16 texel = 0;
            if (textured) {
                int u = g.tex_flip_x ? u0 - x : u0 + x;
                int v = g.tex_flip_y ? v0 - y : v0 + y;
                texel = sample(&ds, u, v);
            }
            for (int j = 0; j < s; j++) {
                for (int k = 0; k < s; k++) {
                    plot(&ds, (x0 + dx) * s + k, (y0 + y) * s + j, r, gg, b, textured, texel);
                }
            }
        }
    }
}

static void gp0_fill(void) {
    u32 c = g.fifo[0];
    u16 color =
        (u16)(((c & 0xFF) >> 3) | (((c >> 8) & 0xFF) >> 3) << 5 | (((c >> 16) & 0xFF) >> 3) << 10);
    int s = g.scale;
    mark_written((int)(g.fifo[1] & 0x3F0), (int)((g.fifo[1] >> 16) & 0x1FF),
                 (int)(((g.fifo[2] & 0x3FF) + 0xF) & ~0xFu), (int)((g.fifo[2] >> 16) & 0x1FF));
    int x0 = (int)(g.fifo[1] & 0x3F0) * s, y0 = (int)((g.fifo[1] >> 16) & 0x1FF) * s;
    int w = (int)(((g.fifo[2] & 0x3FF) + 0xF) & ~0xFu) * s,
        h = (int)((g.fifo[2] >> 16) & 0x1FF) * s;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            *hpx(x0 + x, y0 + y) = color;
        }
    }
}

static void gp0_copy(void) {
    int s = g.scale;
    int sx = (int)(g.fifo[1] & 0x3FF) * s, sy = (int)((g.fifo[1] >> 16) & 0x1FF) * s;
    int dx = (int)(g.fifo[2] & 0x3FF) * s, dy = (int)((g.fifo[2] >> 16) & 0x1FF) * s;
    int w = (int)(g.fifo[3] & 0x3FF), h = (int)((g.fifo[3] >> 16) & 0x1FF);
    mark_written((int)(g.fifo[2] & 0x3FF), (int)((g.fifo[2] >> 16) & 0x1FF), w ? w : 1024,
                 h ? h : 512);
    w = (w ? w : 1024) * s;
    h = (h ? h : 512) * s;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u16 *d = hpx(dx + x, dy + y);
            if (g.mask_check && (*d & 0x8000)) {
                continue;
            }
            *d = (u16)(*hpx(sx + x, sy + y) | (g.mask_set ? 0x8000 : 0));
        }
    }
}

static void begin_transfer(bool upload) {
    g.xfer_x = g.fifo[1] & 0x3FF;
    g.xfer_y = (g.fifo[1] >> 16) & 0x1FF;
    g.xfer_w = g.fifo[2] & 0x3FF;
    g.xfer_h = (g.fifo[2] >> 16) & 0x1FF;
    g.xfer_w = g.xfer_w ? g.xfer_w : 1024;
    g.xfer_h = g.xfer_h ? g.xfer_h : 512;
    g.xfer_i = 0;
    if (upload) {
        mark_written((int)g.xfer_x, (int)g.xfer_y, (int)g.xfer_w, (int)g.xfer_h);
    }
    g.uploading = upload;
    g.downloading = !upload;
}

static void xfer_pixel(u16 value) {
    int x = (int)(g.xfer_x + g.xfer_i % g.xfer_w), y = (int)(g.xfer_y + g.xfer_i / g.xfer_w);
    if (!(g.mask_check && (*px(x, y) & 0x8000))) {
        put_native(x, y, (u16)(value | (g.mask_set ? 0x8000 : 0)));
    }
    if (++g.xfer_i >= g.xfer_w * g.xfer_h) {
        g.uploading = false;
    }
}

/* --- command dispatch -------------------------------------------------------------------- */

static u32 command_words(u32 op) {
    switch (op >> 5) {
    case 1: { /* polygon */
        u32 verts = (op & 0x08) ? 4 : 3;
        u32 per = 1 + ((op & 0x04) ? 1 : 0) + ((op & 0x10) ? 1 : 0);
        return 1 + verts * per - ((op & 0x10) ? 1 : 0);
    }
    case 2:
        return (op & 0x10) ? 4 : 3;
    case 3: {
        u32 n = 2 + ((op & 0x04) ? 1 : 0);
        return ((op >> 3) & 3) == 0 ? n + 1 : n;
    }
    case 4:
        return 4;
    case 5:
    case 6:
        return 3;
    default:
        return op == 0x02 ? 3 : 1;
    }
}

static void environment(u32 w) {
    switch (w >> 24) {
    case 0xE1:
        g.texpage = w & 0x3FFF;
        g.tex_flip_x = (w >> 12) & 1;
        g.tex_flip_y = (w >> 13) & 1;
        break;
    case 0xE2:
        g.tex_window = w & 0xFFFFF;
        break;
    case 0xE3:
        g.clip_x1 = (int)(w & 0x3FF);
        g.clip_y1 = (int)((w >> 10) & 0x1FF);
        break;
    case 0xE4:
        g.clip_x2 = (int)(w & 0x3FF);
        g.clip_y2 = (int)((w >> 10) & 0x1FF);
        break;
    case 0xE5:
        g.off_x = sext11(w);
        g.off_y = sext11(w >> 11);
        break;
    case 0xE6:
        g.mask_set = w & 1;
        g.mask_check = (w >> 1) & 1;
        break;
    default:
        break;
    }
}

static void execute(void) {
    u32 op = g.fifo[0] >> 24;
    switch (op >> 5) {
    case 1:
        gp0_polygon();
        break;
    case 2:
        gp0_line();
        if (op & 0x08) { /* polyline: keep consuming vertices until the terminator */
            g.polyline = true;
            g.polyline_have_color = false;
        }
        break;
    case 3:
        gp0_rect();
        break;
    case 4:
        gp0_copy();
        break;
    case 5:
        begin_transfer(true);
        break;
    case 6:
        begin_transfer(false);
        break;
    default:
        if (op == 0x02) {
            gp0_fill();
        } else if (op >= 0xE1 && op <= 0xE6) {
            environment(g.fifo[0]);
        }
        break;
    }
}

/* Polyline continuation: each further vertex draws a segment from the previous end point. */
static void polyline_word(u32 word) {
    if ((word & 0xF000F000u) == 0x50005000u) {
        g.polyline = false;
        return;
    }
    bool gouraud = (g.fifo[0] >> 24) & 0x10;
    if (gouraud) {
        if (!g.polyline_have_color) {
            g.polyline_color = word;
            g.polyline_have_color = true;
            return;
        }
        g.polyline_have_color = false;
        g.fifo[0] = (g.fifo[0] & 0xFF000000u) | (g.fifo[2] & 0xFFFFFF);
        g.fifo[1] = g.fifo[3];
        g.fifo[2] = g.polyline_color;
        g.fifo[3] = word;
    } else {
        g.fifo[1] = g.fifo[2];
        g.fifo[2] = word;
    }
    gp0_line();
}

void gpu_gp0(u32 word) {
    ensure_vram();
    if (g.uploading) {
        xfer_pixel((u16)word);
        if (g.uploading) {
            xfer_pixel((u16)(word >> 16));
        }
        return;
    }
    if (g.polyline) {
        polyline_word(word);
        return;
    }
    if (g.fifo_len == 0) {
        g.fifo_need = command_words(word >> 24);
    }
    g.fifo[g.fifo_len++] = word;
    if (g.fifo_len == g.fifo_need) {
        execute();
        g.fifo_len = 0;
    }
}

void gpu_gp1(u32 word) {
    switch (word >> 24) {
    case 0x00: /* reset */
        g.fifo_len = 0;
        g.uploading = g.downloading = g.polyline = false;
        g.texpage = 0;
        g.disp_off = true;
        g.dma_dir = 0;
        break;
    case 0x01:
        g.fifo_len = 0;
        g.uploading = g.polyline = false;
        break;
    case 0x03:
        g.disp_off = word & 1;
        break;
    case 0x04:
        g.dma_dir = word & 3;
        break;
    case 0x05:
        g.flips++;
        ws_end_frame();
        g.frame_has_3d = g.polys3d_since_flip > 0;
        g.polys3d_since_flip = 0;
        g.vblanks_since_flip = 0;
        g.disp_x = word & 0x3FE;
        g.disp_y = (word >> 10) & 0x1FF;
        break;
    case 0x06:
        g.h_start = word & 0xFFF;
        g.h_end = (word >> 12) & 0xFFF;
        break;
    case 0x07:
        g.v_start = word & 0x3FF;
        g.v_end = (word >> 10) & 0x3FF;
        break;
    case 0x08:
        g.disp_mode = word & 0xFF;
        break;
    default:
        break;
    }
}

u32 gpu_read(void) {
    ensure_vram();
    if (!g.downloading) {
        return 0;
    }
    u32 w = 0;
    for (int i = 0; i < 2; i++) {
        u32 x = g.xfer_x + g.xfer_i % g.xfer_w, y = g.xfer_y + g.xfer_i / g.xfer_w;
        w |= (u32)*px((int)x, (int)y) << (16 * i);
        if (++g.xfer_i >= g.xfer_w * g.xfer_h) {
            g.downloading = false;
            break;
        }
    }
    return w;
}

u32 gpu_status(void) {
    u32 s = g.texpage & 0x7FF;
    s |= g.mask_set ? 1u << 11 : 0;
    s |= g.mask_check ? 1u << 12 : 0;
    s |= 1u << 13; /* interlace field */
    s |= (g.disp_mode & 0x3Fu) << 17;
    s |= ((g.disp_mode >> 6) & 1u) << 16;
    s |= g.disp_off ? 1u << 23 : 0;
    s |= 0x1C000000u; /* ready for command, VRAM->CPU, DMA */
    s |= g.dma_dir << 29;
    if (g.dma_dir == 1 || g.dma_dir == 2) {
        s |= 1u << 25;
    } else if (g.dma_dir == 3) {
        s |= g.downloading ? 1u << 25 : 0;
    }
    s |= g.odd_line ? 1u << 31 : 0;
    return s;
}

void gpu_vblank(void) {
    g.frame++;
    /* A single-buffered still picture never flips: decide from what was drawn. */
    if (++g.vblanks_since_flip > 15) {
        ws_end_frame();
        g.frame_has_3d = g.polys3d_since_flip > 0;
    }
    g.odd_line = !g.odd_line;
}

bool gpu_frame_has_3d(void) {
    return g.frame_has_3d;
}

u32 gpu_flip_count(void) {
    return g.flips;
}

/* --- display ------------------------------------------------------------------------------ */

void gpu_display_info(GpuDisplay *out) {
    ensure_vram();
    static const u32 widths[4] = {256, 320, 512, 640};
    u32 w = (g.disp_mode & 0x40) ? 368 : widths[g.disp_mode & 3];
    u32 h = (g.disp_mode & 0x04) && (g.disp_mode & 0x20) ? 480 : 240;
    u32 lines = g.v_end > g.v_start ? g.v_end - g.v_start : 240;
    if (h == 480) {
        lines *= 2;
    }
    out->x = g.disp_x;
    out->y = g.disp_y;
    out->width = w;
    out->height = lines < h ? lines : h;
    out->rgb24 = (g.disp_mode & 0x10) != 0;
    out->enabled = !g.disp_off;
    /* 24-bit (movie) output is packed bytes, so it is shown at native resolution. */
    out->scale = out->rgb24 ? 1u : (u32)g.scale;
}

void gpu_display_rgba(u32 *dst, const GpuDisplay *d) {
    u32 s = d->scale, ow = d->width * s, oh = d->height * s;
    for (u32 y = 0; y < oh; y++) {
        for (u32 x = 0; x < ow; x++) {
            u32 r, gg, b;
            if (d->rgb24) {
                /* Bytes packed across native pixels: gather from block top-lefts. */
                u32 byte = d->x * 2 + x * 3;
                u8 bytes[3];
                for (int k = 0; k < 3; k++) {
                    u32 bi = byte + (u32)k;
                    u16 pix = *px((int)((bi / 2) % VRAM_W), (int)(d->y + y));
                    bytes[k] = (u8)(bi & 1 ? pix >> 8 : pix);
                }
                r = bytes[0];
                gg = bytes[1];
                b = bytes[2];
            } else {
                u16 p = *hpx((int)(d->x * s + x), (int)(d->y * s + y));
                r = (u32)(p & 0x1F) << 3 | (p & 0x1F) >> 2;
                gg = (u32)((p >> 5) & 0x1F) << 3 | ((p >> 5) & 0x1F) >> 2;
                b = (u32)((p >> 10) & 0x1F) << 3 | ((p >> 10) & 0x1F) >> 2;
            }
            dst[y * ow + x] = 0xFF000000u | b << 16 | gg << 8 | r;
        }
    }
}

/* VRAM is stored at the internal resolution it was saved with; loading switches to that scale
 * (the caller then re-applies the user's setting, which resamples). */
void gpu_serialize(StateIO *io) {
    ensure_vram();
    u16 *vram = g.vram;
    int scale = g.scale;
    STATE_VAR(io, g);
    if (io->loading) {
        tex_clear(); /* VRAM is replaced: cached upscaled textures are stale */
        int saved_scale = g.scale;
        g.vram = vram;
        g.scale = scale;
        gpu_set_scale(saved_scale);
    } else {
        g.vram = vram;
    }
    state_io(io, g.vram, (size_t)VRAM_W * VRAM_H * (size_t)(g.scale * g.scale) * sizeof(u16));
}

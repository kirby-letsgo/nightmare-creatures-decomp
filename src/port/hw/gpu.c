/* Software GPU: GP0/GP1 command processing into a 1024x512 15-bit VRAM, with a reference
 * rasterizer for polygons, lines and rectangles (flat/gouraud, textured 4/8/15-bit,
 * semi-transparency, dithering, mask bit). This is the accurate 1x path; the upscaling
 * SDL_GPU renderer consumes the same command stream later.
 * Reference: psx-spx "GPU". */
#include "port/hw/gpu.h"

#include "port/hw/hw.h"
#include "port/runtime.h"

#include <string.h>

#define VRAM_W 1024
#define VRAM_H 512

typedef struct GpuState {
    u16 vram[VRAM_W * VRAM_H];

    /* GP0 command assembly */
    u32 fifo[16];
    u32 fifo_len, fifo_need;
    bool polyline;
    bool polyline_have_color;
    u32 polyline_color;

    /* CPU<->VRAM transfers */
    bool uploading, downloading;
    u32 xfer_x, xfer_y, xfer_w, xfer_h, xfer_i;

    /* drawing environment */
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
} GpuState;

static GpuState g = {.disp_off = true};

/* --- helpers ------------------------------------------------------------------------------ */

static int sext11(u32 v) {
    return (int)((v & 0x7FFu) ^ 0x400u) - 0x400;
}

static u16 *px(int x, int y) {
    return &g.vram[(y & (VRAM_H - 1)) * VRAM_W + (x & (VRAM_W - 1))];
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
    int x, y;
    int r, g, b;
    int u, v;
} Vertex;

typedef struct DrawState {
    bool textured, raw, semi, gouraud, dither;
    u32 clut_x, clut_y;
    u32 tp_x, tp_y, tp_depth, blend;
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

static void plot(const DrawState *ds, int x, int y, int r, int gg, int b, bool textured_px,
                 u16 texel) {
    if (x < g.clip_x1 || x > g.clip_x2 || y < g.clip_y1 || y > g.clip_y2) {
        return;
    }
    u16 *dst = px(x, y);
    if (g.mask_check && (*dst & 0x8000)) {
        return;
    }
    bool semi = ds->semi;
    if (textured_px) {
        if (texel == 0) {
            return; /* fully transparent */
        }
        int tr = texel & 0x1F, tg = (texel >> 5) & 0x1F, tb = (texel >> 10) & 0x1F;
        if (ds->raw) {
            r = tr << 3;
            gg = tg << 3;
            b = tb << 3;
        } else {
            r = (tr << 3) * r / 128;
            gg = (tg << 3) * gg / 128;
            b = (tb << 3) * b / 128;
        }
        semi = semi && (texel & 0x8000);
    }
    if (ds->dither) {
        int d = dither[y & 3][x & 3];
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
    u16 mask = (g.mask_set || (textured_px && (texel & 0x8000))) ? 0x8000 : 0;
    *dst = (u16)(r5 | (g5 << 5) | (b5 << 10) | mask);
}

/* --- triangles ------------------------------------------------------------------------ */

static s64 edge(const Vertex *a, const Vertex *b, int x, int y) {
    return (s64)(b->x - a->x) * (y - a->y) - (s64)(b->y - a->y) * (x - a->x);
}

/* Top-left fill rule: pixels exactly on an edge belong to it only for top/left edges. */
static bool is_top_left(const Vertex *a, const Vertex *b) {
    return (a->y == b->y && b->x < a->x) || (b->y < a->y);
}

static void draw_triangle(const DrawState *ds, Vertex v0, Vertex v1, Vertex v2) {
    s64 area = edge(&v0, &v1, v2.x, v2.y);
    if (area == 0) {
        return;
    }
    if (area < 0) {
        Vertex t = v1;
        v1 = v2;
        v2 = t;
        area = -area;
    }
    /* Hardware rejects polygons larger than 1023x511. */
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
    minx = minx < g.clip_x1 ? g.clip_x1 : minx;
    miny = miny < g.clip_y1 ? g.clip_y1 : miny;
    maxx = maxx > g.clip_x2 ? g.clip_x2 : maxx;
    maxy = maxy > g.clip_y2 ? g.clip_y2 : maxy;

    int bias0 = is_top_left(&v1, &v2) ? 0 : -1;
    int bias1 = is_top_left(&v2, &v0) ? 0 : -1;
    int bias2 = is_top_left(&v0, &v1) ? 0 : -1;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            s64 w0 = edge(&v1, &v2, x, y), w1 = edge(&v2, &v0, x, y), w2 = edge(&v0, &v1, x, y);
            if (w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) {
                continue;
            }
            int r = v0.r, gg = v0.g, b = v0.b;
            if (ds->gouraud) {
                r = (int)((w0 * v0.r + w1 * v1.r + w2 * v2.r) / area);
                gg = (int)((w0 * v0.g + w1 * v1.g + w2 * v2.g) / area);
                b = (int)((w0 * v0.b + w1 * v1.b + w2 * v2.b) / area);
            }
            if (ds->textured) {
                int u = (int)((w0 * v0.u + w1 * v1.u + w2 * v2.u) / area);
                int v = (int)((w0 * v0.v + w1 * v1.v + w2 * v2.v) / area);
                plot(ds, x, y, r, gg, b, true, sample(ds, u, v));
            } else {
                plot(ds, x, y, r, gg, b, false, 0);
            }
        }
    }
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
    u32 i = 0;
    u32 color = 0;
    int nverts = quad ? 4 : 3;
    for (int n = 0; n < nverts; n++) {
        if (n == 0 || gouraud) {
            color = g.fifo[i++];
        }
        u32 xy = g.fifo[i++];
        v[n].x = sext11(xy) + g.off_x;
        v[n].y = sext11(xy >> 16) + g.off_y;
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
    draw_triangle(&ds, v[0], v[1], v[2]);
    if (quad) {
        draw_triangle(&ds, v[1], v[2], v[3]);
    }
}

/* --- lines and rectangles ---------------------------------------------------------------- */

static int iabs(int v) {
    return v < 0 ? -v : v;
}

static void draw_line(const DrawState *ds, Vertex a, Vertex b) {
    int dx = b.x - a.x, dy = b.y - a.y;
    if (iabs(dx) >= 1024 || iabs(dy) >= 512) {
        return;
    }
    int steps = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
    for (int i = 0; i <= steps; i++) {
        int x = steps ? a.x + dx * i / steps : a.x;
        int y = steps ? a.y + dy * i / steps : a.y;
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

static void gp0_rect(void) {
    u32 op = g.fifo[0] >> 24;
    bool textured = op & 0x04;
    DrawState ds = {.textured = textured, .raw = op & 0x01, .semi = op & 0x02};
    apply_texpage(&ds, g.texpage);
    u32 i = 1;
    u32 xy = g.fifo[i++];
    int x0 = sext11(xy) + g.off_x, y0 = sext11(xy >> 16) + g.off_y;
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
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (textured) {
                int u = g.tex_flip_x ? u0 - x : u0 + x;
                int v = g.tex_flip_y ? v0 - y : v0 + y;
                plot(&ds, x0 + x, y0 + y, r, gg, b, true, sample(&ds, u, v));
            } else {
                plot(&ds, x0 + x, y0 + y, r, gg, b, false, 0);
            }
        }
    }
}

static void gp0_fill(void) {
    u32 c = g.fifo[0];
    u16 color =
        (u16)(((c & 0xFF) >> 3) | (((c >> 8) & 0xFF) >> 3) << 5 | (((c >> 16) & 0xFF) >> 3) << 10);
    u32 x0 = g.fifo[1] & 0x3F0, y0 = (g.fifo[1] >> 16) & 0x1FF;
    u32 w = ((g.fifo[2] & 0x3FF) + 0xF) & ~0xFu, h = (g.fifo[2] >> 16) & 0x1FF;
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            *px((int)(x0 + x), (int)(y0 + y)) = color;
        }
    }
}

static void gp0_copy(void) {
    u32 sx = g.fifo[1] & 0x3FF, sy = (g.fifo[1] >> 16) & 0x1FF;
    u32 dx = g.fifo[2] & 0x3FF, dy = (g.fifo[2] >> 16) & 0x1FF;
    u32 w = g.fifo[3] & 0x3FF, h = (g.fifo[3] >> 16) & 0x1FF;
    w = w ? w : 1024;
    h = h ? h : 512;
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            u16 *d = px((int)(dx + x), (int)(dy + y));
            if (g.mask_check && (*d & 0x8000)) {
                continue;
            }
            *d = (u16)(*px((int)(sx + x), (int)(sy + y)) | (g.mask_set ? 0x8000 : 0));
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
    g.uploading = upload;
    g.downloading = !upload;
}

static void xfer_pixel(u16 value) {
    u32 x = g.xfer_x + g.xfer_i % g.xfer_w, y = g.xfer_y + g.xfer_i / g.xfer_w;
    u16 *d = px((int)x, (int)y);
    if (!(g.mask_check && (*d & 0x8000))) {
        *d = (u16)(value | (g.mask_set ? 0x8000 : 0));
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
    g.odd_line = !g.odd_line;
}

/* --- display ------------------------------------------------------------------------------ */

void gpu_display_info(GpuDisplay *out) {
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
}

void gpu_display_rgba(u32 *dst, const GpuDisplay *d) {
    for (u32 y = 0; y < d->height; y++) {
        for (u32 x = 0; x < d->width; x++) {
            u32 r, gg, b;
            if (d->rgb24) {
                const u8 *row = (const u8 *)px(0, (int)(d->y + y));
                u32 byte = d->x * 2 + x * 3;
                r = row[byte % (VRAM_W * 2)];
                gg = row[(byte + 1) % (VRAM_W * 2)];
                b = row[(byte + 2) % (VRAM_W * 2)];
            } else {
                u16 p = *px((int)(d->x + x), (int)(d->y + y));
                r = (u32)(p & 0x1F) << 3 | (p & 0x1F) >> 2;
                gg = (u32)((p >> 5) & 0x1F) << 3 | ((p >> 5) & 0x1F) >> 2;
                b = (u32)((p >> 10) & 0x1F) << 3 | ((p >> 10) & 0x1F) >> 2;
            }
            dst[y * d->width + x] = 0xFF000000u | b << 16 | gg << 8 | r;
        }
    }
}

const u16 *gpu_vram(void) {
    return g.vram;
}

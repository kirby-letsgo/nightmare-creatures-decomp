#include "port/hw/widescreen.h"

#include <string.h>

#define SLOTS 16384u /* power of two; a frame projects a few thousand vertices */
/* Projected positions stay valid for this many "active" VBlanks: ones in which the GTE did any
 * projection. When the game stops projecting (pause menu redrawing the last 3D frame), the
 * table does not age, so the frozen scene is still recognised as 3D. */
#define MAX_AGE 4u

typedef struct Entry {
    u32 key;   /* (y << 16) | (x & 0xFFFF) */
    u32 stamp; /* vblank count when recorded; 0 = empty */
} Entry;

static bool enabled;
static Entry table[SLOTS];
static u32 now = 1;
static bool projected_this_vblank;

void ws_set_enabled(bool on) {
    enabled = on;
    if (!on) {
        memset(table, 0, sizeof table);
    }
}

bool ws_enabled(void) {
    return enabled;
}

static u32 make_key(int x, int y) {
    return ((u32)(y & 0xFFFF) << 16) | (u32)(x & 0xFFFF);
}

static u32 slot_of(u32 key) {
    return (key * 2654435761u) >> 18; /* top 14 bits */
}

static bool fresh(const Entry *e) {
    return e->stamp != 0 && now - e->stamp <= MAX_AGE;
}

void ws_note_projected(int x, int y) {
    if (!enabled) {
        return;
    }
    projected_this_vblank = true;
    u32 key = make_key(x, y);
    for (u32 i = slot_of(key), n = 0; n < 16; i = (i + 1) & (SLOTS - 1), n++) {
        Entry *e = &table[i];
        if (!fresh(e) || e->key == key) {
            e->key = key;
            e->stamp = now;
            return;
        }
    }
    /* Neighbourhood full: overwrite the home slot. */
    table[slot_of(key)] = (Entry){key, now};
}

bool ws_is_projected(int x, int y) {
    u32 key = make_key(x, y);
    for (u32 i = slot_of(key), n = 0; n < 16; i = (i + 1) & (SLOTS - 1), n++) {
        const Entry *e = &table[i];
        if (fresh(e) && e->key == key) {
            return true;
        }
    }
    return false;
}

void ws_vblank(void) {
    if (!projected_this_vblank) {
        return;
    }
    projected_this_vblank = false;
    now++;
    if (now == 0) {
        now = 1;
        memset(table, 0, sizeof table);
    }
}

/* --- 2D layout -------------------------------------------------------------------------- */

#define MAX_BOXES 4096
#define MAX_CLUSTERS 256
#define CLUSTER_GAP 6 /* px: boxes closer than this belong to the same element */
/* Big 2D shapes (menu backdrops like the pause screen's blood splat) form their own cluster
 * and don't pull nearby HUD elements into it. */
#define BACKDROP_SIZE 96
#define SCREEN_WIDTH 320

/* Anchor offsets after X *= 3/4: left edge stays at 0, right edge stays at 320. */
#define OFFSET_LEFT 0
#define OFFSET_CENTER 40
#define OFFSET_RIGHT 80

typedef struct Box {
    s16 x0, y0, x1, y1;
} Box;

static Box boxes[MAX_BOXES];
static int nboxes;
static int parent[MAX_BOXES];

typedef struct Cluster {
    Box box;
    int offset;
} Cluster;

static Cluster clusters[MAX_CLUSTERS];
static int nclusters;

void ws_record_2d(int minx, int miny, int maxx, int maxy) {
    if (!enabled || nboxes >= MAX_BOXES) {
        return;
    }
    boxes[nboxes++] = (Box){(s16)minx, (s16)miny, (s16)maxx, (s16)maxy};
}

static int find(int i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

static bool is_backdrop(const Box *b) {
    return b->x1 - b->x0 >= BACKDROP_SIZE || b->y1 - b->y0 >= BACKDROP_SIZE;
}

static bool near(const Box *a, const Box *b) {
    if (is_backdrop(a) || is_backdrop(b)) {
        return false;
    }
    return a->x0 <= b->x1 + CLUSTER_GAP && b->x0 <= a->x1 + CLUSTER_GAP &&
           a->y0 <= b->y1 + CLUSTER_GAP && b->y0 <= a->y1 + CLUSTER_GAP;
}

void ws_end_frame(void) {
    if (!enabled) {
        return;
    }
    if (nboxes == 0) {
        return; /* nothing 2D drawn (e.g. frame not finished): keep the previous layout */
    }
    for (int i = 0; i < nboxes; i++) {
        parent[i] = i;
    }
    for (int i = 0; i < nboxes; i++) {
        for (int j = i + 1; j < nboxes; j++) {
            if (near(&boxes[i], &boxes[j])) {
                int a = find(i), b = find(j);
                if (a != b) {
                    parent[a] = b;
                }
            }
        }
    }
    /* Merge each group's boxes into its root, then emit clusters. */
    for (int i = 0; i < nboxes; i++) {
        int r = find(i);
        if (r == i) {
            continue;
        }
        Box *R = &boxes[r];
        const Box *B = &boxes[i];
        R->x0 = B->x0 < R->x0 ? B->x0 : R->x0;
        R->y0 = B->y0 < R->y0 ? B->y0 : R->y0;
        R->x1 = B->x1 > R->x1 ? B->x1 : R->x1;
        R->y1 = B->y1 > R->y1 ? B->y1 : R->y1;
    }
    nclusters = 0;
    for (int i = 0; i < nboxes && nclusters < MAX_CLUSTERS; i++) {
        if (find(i) != i) {
            continue;
        }
        const Box *b = &boxes[i];
        int offset = OFFSET_CENTER;
        if (b->x1 < SCREEN_WIDTH / 2) {
            offset = OFFSET_LEFT;
        } else if (b->x0 > SCREEN_WIDTH / 2) {
            offset = OFFSET_RIGHT;
        }
        clusters[nclusters++] = (Cluster){*b, offset};
    }
    nboxes = 0;
}

int ws_anchor_offset(int minx, int miny, int maxx, int maxy) {
    int cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    for (int i = 0; i < nclusters; i++) {
        const Box *b = &clusters[i].box;
        if (cx >= b->x0 && cx <= b->x1 && cy >= b->y0 && cy <= b->y1) {
            return clusters[i].offset;
        }
    }
    /* Not seen last frame: decide from the primitive itself. */
    if (maxx < SCREEN_WIDTH / 2) {
        return OFFSET_LEFT;
    }
    if (minx > SCREEN_WIDTH / 2) {
        return OFFSET_RIGHT;
    }
    return OFFSET_CENTER;
}

int ws_squeeze_x(int x, int offset) {
    return (x * 3 + (x >= 0 ? 2 : -2)) / 4 + offset;
}

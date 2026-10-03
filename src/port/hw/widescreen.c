#include "port/hw/widescreen.h"

#include <string.h>

#define SLOTS 16384u /* power of two; a frame projects a few thousand vertices */
/* Projected positions stay valid for this many "active" VBlanks: ones in which the GTE did any
 * projection. When the game stops projecting (pause menu redrawing the last 3D frame), the
 * table does not age, so the frozen scene is still recognised as 3D. */
#define MAX_AGE 4u
#define SCREEN_CENTER_X 160

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

int ws_squeeze_x(int x) {
    int d = x - SCREEN_CENTER_X;
    return SCREEN_CENTER_X + (d * 3 + (d >= 0 ? 2 : -2)) / 4;
}

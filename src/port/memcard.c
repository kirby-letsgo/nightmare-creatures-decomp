/* Memory cards as standard 128 KiB raw images (.mcd, the format emulators use), with the BIOS
 * card filesystem on top: 15 data blocks of 8 KiB, directory entries in block 0.
 * Reference: psx-spx "Memory Card Data Format". */
#include "port/memcard.h"

#include "port/runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define CARD_SIZE 0x20000u
#define FRAME 128u
#define BLOCK 0x2000u
#define BLOCKS 16

enum {
    DIR_FIRST = 0x51,
    DIR_MIDDLE = 0x52,
    DIR_LAST = 0x53,
    DIR_FREE = 0xA0,
};

typedef struct Card {
    bool present;
    u8 data[CARD_SIZE];
    char path[512];
} Card;

static Card cards[2];

static u8 *frame_ptr(Card *c, u32 frame) {
    return c->data + (frame & 0x3FF) * FRAME;
}

static void fix_checksum(u8 *frame) {
    u8 x = 0;
    for (int i = 0; i < 0x7F; i++) {
        x ^= frame[i];
    }
    frame[0x7F] = x;
}

static void format(Card *c) {
    memset(c->data, 0, CARD_SIZE);
    u8 *hdr = frame_ptr(c, 0);
    hdr[0] = 'M';
    hdr[1] = 'C';
    fix_checksum(hdr);
    for (u32 i = 1; i < 16; i++) {
        u8 *d = frame_ptr(c, i);
        d[0] = DIR_FREE;
        d[8] = d[9] = 0xFF;
        fix_checksum(d);
    }
    for (u32 i = 16; i < 36; i++) { /* broken sector list: none */
        u8 *d = frame_ptr(c, i);
        memset(d, 0xFF, 4);
        d[8] = d[9] = 0xFF;
        fix_checksum(d);
    }
    memcpy(frame_ptr(c, 63), hdr, FRAME);
}

static void save(Card *c) {
    FILE *f = fopen(c->path, "wb");
    if (f == NULL) {
        NC_LOG("memcard: cannot write %s", c->path);
        return;
    }
    fwrite(c->data, 1, CARD_SIZE, f);
    fclose(f);
}

void memcard_init(const char *dir) {
    for (int port = 0; port < 2; port++) {
        Card *c = &cards[port];
        snprintf(c->path, sizeof c->path, "%scard%d.mcd", dir, port + 1);
        FILE *f = fopen(c->path, "rb");
        if (f != NULL && fread(c->data, 1, CARD_SIZE, f) == CARD_SIZE) {
            c->present = true;
            NC_LOG("memcard: slot %d = %s", port + 1, c->path);
        } else if (port == 0) {
            /* Slot 1 always has a card; a fresh one is created formatted. */
            format(c);
            c->present = true;
            save(c);
            NC_LOG("memcard: created %s", c->path);
        }
        if (f != NULL) {
            fclose(f);
        }
    }
}

bool memcard_present(int port) {
    return port >= 0 && port < 2 && cards[port].present;
}

bool memcard_read_frame(int port, u32 frame, u8 out[128]) {
    if (!memcard_present(port) || frame >= CARD_SIZE / FRAME) {
        return false;
    }
    memcpy(out, frame_ptr(&cards[port], frame), FRAME);
    return true;
}

bool memcard_write_frame(int port, u32 frame, const u8 in[128]) {
    if (!memcard_present(port) || frame >= CARD_SIZE / FRAME) {
        return false;
    }
    memcpy(frame_ptr(&cards[port], frame), in, FRAME);
    save(&cards[port]);
    return true;
}

/* --- filesystem ------------------------------------------------------------------------- */

static bool name_eq(const u8 *dir_name, const char *name) {
    for (int i = 0; i < 20; i++) {
        char a = (char)dir_name[i], b = name[i];
        if (toupper((unsigned char)a) != toupper((unsigned char)b)) {
            return false;
        }
        if (a == '\0') {
            return true;
        }
    }
    return true;
}

/* Shell-style match supporting '*' and '?' (the BIOS supports both). */
static bool wild_match(const char *pat, const char *s) {
    if (*pat == '\0') {
        return *s == '\0';
    }
    if (*pat == '*') {
        return wild_match(pat + 1, s) || (*s != '\0' && wild_match(pat, s + 1));
    }
    if (*s == '\0') {
        return false;
    }
    return (*pat == '?' || toupper((unsigned char)*pat) == toupper((unsigned char)*s)) &&
           wild_match(pat + 1, s + 1);
}

static int find_file(Card *c, const char *name) {
    for (int b = 1; b < BLOCKS; b++) {
        const u8 *d = frame_ptr(c, (u32)b);
        if (d[0] == DIR_FIRST && name_eq(d + 0x0A, name)) {
            return b;
        }
    }
    return -1;
}

static int next_block(Card *c, int b) {
    const u8 *d = frame_ptr(c, (u32)b);
    u16 next = (u16)(d[8] | d[9] << 8);
    return next == 0xFFFF ? -1 : next + 1;
}

int memcard_file_size(int port, const char *name) {
    if (!memcard_present(port)) {
        return -1;
    }
    Card *c = &cards[port];
    int b = find_file(c, name);
    if (b < 0) {
        return -1;
    }
    const u8 *d = frame_ptr(c, (u32)b);
    return (int)(d[4] | d[5] << 8 | d[6] << 16 | (u32)d[7] << 24);
}

bool memcard_create(int port, const char *name, u32 blocks) {
    if (!memcard_present(port) || blocks == 0 || blocks > 15) {
        return false;
    }
    Card *c = &cards[port];
    if (find_file(c, name) >= 0) {
        return false;
    }
    int free_blocks[15], nfree = 0;
    for (int b = 1; b < BLOCKS; b++) {
        if ((frame_ptr(c, (u32)b)[0] & 0xF0) == 0xA0) {
            free_blocks[nfree++] = b;
        }
    }
    if ((u32)nfree < blocks) {
        return false;
    }
    for (u32 i = 0; i < blocks; i++) {
        u8 *d = frame_ptr(c, (u32)free_blocks[i]);
        memset(d, 0, FRAME);
        d[0] = blocks == 1 ? DIR_FIRST
                           : (i == 0 ? DIR_FIRST : (i == blocks - 1 ? DIR_LAST : DIR_MIDDLE));
        if (i == 0) {
            u32 size = blocks * BLOCK;
            d[4] = (u8)size;
            d[5] = (u8)(size >> 8);
            d[6] = (u8)(size >> 16);
            strncpy((char *)d + 0x0A, name, 20);
        }
        u16 next = i + 1 < blocks ? (u16)(free_blocks[i + 1] - 1) : 0xFFFF;
        d[8] = (u8)next;
        d[9] = (u8)(next >> 8);
        fix_checksum(d);
        memset(c->data + (u32)free_blocks[i] * BLOCK, 0, BLOCK);
    }
    save(c);
    return true;
}

bool memcard_erase(int port, const char *name) {
    if (!memcard_present(port)) {
        return false;
    }
    Card *c = &cards[port];
    int b = find_file(c, name);
    if (b < 0) {
        return false;
    }
    while (b >= 0) {
        u8 *d = frame_ptr(c, (u32)b);
        int next = next_block(c, b);
        d[0] = DIR_FREE;
        d[8] = d[9] = 0xFF;
        fix_checksum(d);
        b = next;
    }
    save(c);
    return true;
}

/* Maps a byte offset within a file to the card offset, following the block chain. */
static int file_offset(Card *c, int first, u32 pos) {
    int b = first;
    for (u32 i = 0; i < pos / BLOCK && b >= 0; i++) {
        b = next_block(c, b);
    }
    return b < 0 ? -1 : (int)((u32)b * BLOCK + pos % BLOCK);
}

int memcard_rw(int port, const char *name, u32 pos, u8 *buf, u32 len, bool write) {
    if (!memcard_present(port)) {
        return -1;
    }
    Card *c = &cards[port];
    int first = find_file(c, name);
    if (first < 0) {
        return -1;
    }
    int size = memcard_file_size(port, name);
    u32 done = 0;
    while (done < len && pos + done < (u32)size) {
        int off = file_offset(c, first, pos + done);
        if (off < 0) {
            break;
        }
        u32 n = BLOCK - (pos + done) % BLOCK;
        n = n < len - done ? n : len - done;
        if (write) {
            memcpy(c->data + off, buf + done, n);
        } else {
            memcpy(buf + done, c->data + off, n);
        }
        done += n;
    }
    if (write && done > 0) {
        save(c);
    }
    return (int)done;
}

int memcard_find(int port, const char *pattern, int after, char name_out[21], u32 *size_out) {
    if (!memcard_present(port)) {
        return -1;
    }
    Card *c = &cards[port];
    for (int b = after + 1; b < BLOCKS; b++) {
        const u8 *d = frame_ptr(c, (u32)b);
        if (d[0] != DIR_FIRST) {
            continue;
        }
        char name[21];
        memcpy(name, d + 0x0A, 20);
        name[20] = '\0';
        if (wild_match(pattern, name)) {
            memcpy(name_out, name, 21);
            *size_out = (u32)(d[4] | d[5] << 8 | d[6] << 16 | (u32)d[7] << 24);
            return b;
        }
    }
    return -1;
}

int memcard_free_blocks(int port) {
    if (!memcard_present(port)) {
        return 0;
    }
    int n = 0;
    for (int b = 1; b < BLOCKS; b++) {
        n += (frame_ptr(&cards[port], (u32)b)[0] & 0xF0) == 0xA0;
    }
    return n;
}

void memcard_format(int port) {
    if (memcard_present(port)) {
        format(&cards[port]);
        save(&cards[port]);
    }
}

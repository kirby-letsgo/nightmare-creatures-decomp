#include "port/disc.h"

#include "port/runtime.h"

#include <libchdr/cdrom.h>
#include <libchdr/chd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHD_FRAME_SIZE 2448 /* 2352 sector + 96 subcode bytes */
#define MODE2_USER_OFFSET 24

static chd_file *chd;
static u8 *hunk_buf;
static u32 hunk_bytes, frames_per_hunk, cached_hunk = 0xFFFFFFFFu;

/* Disc layout from the CHD's CHT2 metadata. Each track's stored frames are padded to a
 * multiple of 4 in the file. A pregap is stored with the track for PGTYPE:V*, otherwise it is
 * silence that exists only on the disc's timeline. */
static DiscTrack tracks[DISC_MAX_TRACKS];
static u32 chd_offset[DISC_MAX_TRACKS]; /* first stored frame of each track */
static bool pregap_stored[DISC_MAX_TRACKS];
static int track_count;

static bool load_toc(void) {
    char meta[256];
    u32 lba = 0, offset = 0;
    track_count = 0;
    for (int i = 0; i < DISC_MAX_TRACKS; i++) {
        u32 len = 0, tag = 0;
        u8 flags = 0;
        if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, (u32)i, meta, sizeof meta - 1, &len,
                             &tag, &flags) != CHDERR_NONE) {
            break;
        }
        meta[len < sizeof meta ? len : sizeof meta - 1] = '\0';
        int number = 0, frames = 0, pregap = 0, postgap = 0;
        char type[32] = "", subtype[32] = "", pgtype[32] = "", pgsub[32] = "";
        if (sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &number, type, subtype, &frames, &pregap,
                   pgtype, pgsub, &postgap) != 8) {
            NC_LOG("disc: unparsable track metadata: %s", meta);
            return false;
        }
        bool stored = pgtype[0] == 'V';
        DiscTrack *t = &tracks[track_count];
        t->audio = strcmp(type, "AUDIO") == 0;
        t->start = lba;
        t->index1 = lba + (u32)pregap;
        t->frames = (u32)frames + (stored ? 0u : (u32)pregap);
        chd_offset[track_count] = offset;
        pregap_stored[track_count] = stored;
        lba += t->frames;
        offset += ((u32)frames + 3u) & ~3u;
        track_count++;
    }
    return track_count > 0;
}

bool disc_open(const char *chd_path) {
    if (chd_open(chd_path, CHD_OPEN_READ, NULL, &chd) != CHDERR_NONE) {
        NC_LOG("cannot open disc image %s", chd_path);
        return false;
    }
    const chd_header *hdr = chd_get_header(chd);
    hunk_bytes = hdr->hunkbytes;
    frames_per_hunk = hunk_bytes / CHD_FRAME_SIZE;
    hunk_buf = malloc(hunk_bytes);
    if (hunk_buf == NULL || !load_toc()) {
        disc_close();
        return false;
    }
    NC_LOG("disc: %d tracks, lead-out at LBA %u", track_count, disc_leadout());
    return true;
}

void disc_close(void) {
    if (chd != NULL) {
        chd_close(chd);
        chd = NULL;
    }
    free(hunk_buf);
    hunk_buf = NULL;
    cached_hunk = 0xFFFFFFFFu;
}

int disc_track_count(void) {
    return track_count;
}

const DiscTrack *disc_track(int number) {
    return (number >= 1 && number <= track_count) ? &tracks[number - 1] : NULL;
}

u32 disc_leadout(void) {
    if (track_count == 0) {
        return 0;
    }
    const DiscTrack *last = &tracks[track_count - 1];
    return last->start + last->frames;
}

bool disc_read_raw(u32 lba, u8 out[DISC_RAW_SECTOR]) {
    int t = track_count - 1;
    while (t > 0 && lba < tracks[t].start) {
        t--;
    }
    if (track_count == 0 || lba >= disc_leadout()) {
        return false;
    }
    if (!pregap_stored[t] && lba < tracks[t].index1) {
        memset(out, 0, DISC_RAW_SECTOR);
        return true;
    }
    u32 first = pregap_stored[t] ? tracks[t].start : tracks[t].index1;
    u32 frame = chd_offset[t] + (lba - first);
    u32 hunk = frame / frames_per_hunk;
    if (hunk != cached_hunk) {
        if (chd_read(chd, hunk, hunk_buf) != CHDERR_NONE) {
            return false;
        }
        cached_hunk = hunk;
    }
    memcpy(out, hunk_buf + (frame % frames_per_hunk) * CHD_FRAME_SIZE, DISC_RAW_SECTOR);
    if (tracks[t].audio) { /* CHD stores CD audio big-endian */
        for (u32 i = 0; i < DISC_RAW_SECTOR; i += 2) {
            u8 tmp = out[i];
            out[i] = out[i + 1];
            out[i + 1] = tmp;
        }
    }
    return true;
}

bool disc_read_data(u32 lba, u8 out[DISC_DATA_SECTOR]) {
    u8 raw[DISC_RAW_SECTOR];
    if (!disc_read_raw(lba, raw)) {
        return false;
    }
    memcpy(out, raw + MODE2_USER_OFFSET, DISC_DATA_SECTOR);
    return true;
}

static u32 le32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* Compares a path component against an ISO name, ignoring case and any ";1" suffix. */
static bool name_matches(const char *want, size_t want_len, const u8 *iso, size_t iso_len) {
    const u8 *semi = memchr(iso, ';', iso_len);
    if (semi != NULL) {
        iso_len = (size_t)(semi - iso);
    }
    if (want_len != iso_len) {
        return false;
    }
    for (size_t i = 0; i < want_len; i++) {
        char a = want[i], b = (char)iso[i];
        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 32);
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static bool find_in_dir(u32 lba, u32 size, const char *name, size_t len, DiscFile *out,
                        bool *is_dir) {
    u8 sector[DISC_DATA_SECTOR];
    for (u32 off = 0; off < size; off += DISC_DATA_SECTOR) {
        if (!disc_read_data(lba + off / DISC_DATA_SECTOR, sector)) {
            return false;
        }
        for (u32 pos = 0; pos < DISC_DATA_SECTOR;) {
            u8 rec_len = sector[pos];
            if (rec_len == 0) {
                break; /* rest of this sector is padding */
            }
            const u8 *rec = sector + pos;
            if (name_matches(name, len, rec + 33, rec[32])) {
                out->lba = le32(rec + 2);
                out->size = le32(rec + 10);
                *is_dir = (rec[25] & 0x02) != 0;
                return true;
            }
            pos += rec_len;
        }
    }
    return false;
}

bool disc_find(const char *path, DiscFile *out) {
    u8 pvd[DISC_DATA_SECTOR];
    if (!disc_read_data(16, pvd)) {
        return false;
    }
    DiscFile cur = {le32(pvd + 156 + 2), le32(pvd + 156 + 10)};

    /* Strip "cdrom:" and leading separators; drop ";1" version suffixes. */
    if (strncmp(path, "cdrom:", 6) == 0) {
        path += 6;
    }
    while (*path == '\\' || *path == '/') {
        path++;
    }
    while (*path != '\0') {
        size_t len = strcspn(path, "\\/;");
        bool is_dir = false;
        if (!find_in_dir(cur.lba, cur.size, path, len, &cur, &is_dir)) {
            return false;
        }
        path += len;
        if (*path == ';') {
            path += strcspn(path, "\\/");
        }
        while (*path == '\\' || *path == '/') {
            path++;
        }
        if (*path != '\0' && !is_dir) {
            return false;
        }
    }
    *out = cur;
    return true;
}

bool disc_read_file(const DiscFile *file, u8 *dst) {
    u8 sector[DISC_DATA_SECTOR];
    for (u32 off = 0; off < file->size; off += DISC_DATA_SECTOR) {
        if (!disc_read_data(file->lba + off / DISC_DATA_SECTOR, sector)) {
            return false;
        }
        u32 n = file->size - off < DISC_DATA_SECTOR ? file->size - off : DISC_DATA_SECTOR;
        memcpy(dst + off, sector, n);
    }
    return true;
}

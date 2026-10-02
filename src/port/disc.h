/* Read-only access to the user's disc image (CHD) and its ISO9660 filesystem. */
#ifndef NC_PORT_DISC_H
#define NC_PORT_DISC_H

#include "port/recomp.h"

#include <stdbool.h>

#define DISC_RAW_SECTOR 2352
#define DISC_DATA_SECTOR 2048

typedef struct DiscFile {
    u32 lba;
    u32 size;
} DiscFile;

#define DISC_MAX_TRACKS 99

typedef struct DiscTrack {
    bool audio;
    u32 start;  /* LBA of index 0 (pregap start) */
    u32 index1; /* LBA of index 1 (track proper) */
    u32 frames; /* including pregap */
} DiscTrack;

bool disc_open(const char *chd_path);
int disc_track_count(void);
/* 1-based track number; NULL if out of range. */
const DiscTrack *disc_track(int number);
/* LBA one past the last sector (start of the lead-out). */
u32 disc_leadout(void);
void disc_close(void);

/* Reads one raw 2352-byte sector. Audio sectors come back as little-endian 16-bit stereo PCM.
 * Returns false past the end of the disc. */
bool disc_read_raw(u32 lba, u8 out[DISC_RAW_SECTOR]);
/* Reads the 2048-byte user data of a MODE2 form-1 sector. */
bool disc_read_data(u32 lba, u8 out[DISC_DATA_SECTOR]);

/* Looks up a file by path ("\\PSX2.EXE;1", "MAP1/FOO.BIN", case-insensitive, version optional). */
bool disc_find(const char *path, DiscFile *out);
/* Reads a whole file's data into dst (which must hold file->size bytes). */
bool disc_read_file(const DiscFile *file, u8 *dst);

#endif

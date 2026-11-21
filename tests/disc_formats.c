/* Compare the raw backend with the normalized 2352-byte sectors presented to the game by CHD. */
#include "port/disc.h"
#include "port/raw_disc.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static bool compare_image(const char *path) {
    RawDisc *raw = NULL;
    DiscCheck result = raw_disc_open(path, &raw);
    if (result != DISC_OK) {
        fprintf(stderr, "Cannot open %s (disc check %d)\n", path, result);
        return false;
    }
    DiscTrack tracks[DISC_MAX_TRACKS];
    bool ok = raw_disc_toc(tracks) == disc_track_count();
    for (int i = 0; ok && i < disc_track_count(); i++) {
        const DiscTrack *ref = disc_track(i + 1);
        DiscTrack *track = &tracks[i];
        ok = track->audio == ref->audio && track->start == ref->start &&
             track->index1 == ref->index1 && track->frames == ref->frames;
        if (!ok)
            fprintf(stderr, "%s: track %d layout differs\n", path, i + 1);
        for (u32 n = 0; ok && n < track->frames; n++) {
            u8 expected[DISC_RAW_SECTOR], actual[DISC_RAW_SECTOR];
            ok = disc_read_raw(ref->start + n, expected) && raw_disc_read(raw, i, n, actual) &&
                 memcmp(expected, actual, sizeof expected) == 0;
            if (!ok)
                fprintf(stderr, "%s: track %d sector %u differs\n", path, i + 1, n);
        }
    }
    raw_disc_close(raw);
    if (ok)
        printf("%s: all %u sectors and %d tracks match\n", path, disc_leadout(),
               disc_track_count());
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 3)
        return 1;
    for (int i = 1; i < argc; i++) {
        if (!SDL_GetPathInfo(argv[i], NULL)) {
            printf("Missing local disc fixture: %s\n", argv[i]);
            return 77;
        }
    }
    if (!disc_open(argv[1]))
        return 1;
    bool ok = true;
    for (int i = 2; ok && i < argc; i++)
        ok = compare_image(argv[i]);
    disc_close();
    return ok ? 0 : 1;
}

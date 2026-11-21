#include "port/raw_disc.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <jni.h>
#endif

/* Sector counts from the verified USA CHD's CHT2 records, including the 150-sector
 * pregap on each audio track. They also identify the TOC of a merged BIN without a sheet. */
static const u32 frames[] = {90494, 29530, 30570, 30021, 29390, 14891, 15002, 16813, 16350,
                             15091, 1073,  1140,  1005,  947,   1230,  1074,  1250,  1218,
                             986,   1236,  1080,  965,   1187,  1225,  1166,  13650};
enum { TRACKS = SDL_arraysize(frames), AUDIO_GAP = 150, MAX_CUE = 65536 };

typedef struct RawTrack {
    SDL_IOStream *io;
    u32 offset, silence;
} RawTrack;

struct RawDisc {
    RawTrack track[TRACKS];
    SDL_IOStream *files[TRACKS];
    int file_count;
};

int raw_disc_toc(DiscTrack *tracks) {
    u32 lba = 0;
    for (int i = 0; i < TRACKS; i++) {
        tracks[i] = (DiscTrack){i != 0, lba, lba + (i ? AUDIO_GAP : 0), frames[i]};
        lba += frames[i];
    }
    return TRACKS;
}

void raw_disc_close(RawDisc *raw) {
    if (raw) {
        for (int i = 0; i < raw->file_count; i++) {
            SDL_CloseIO(raw->files[i]);
        }
        free(raw);
    }
}

bool raw_disc_read(RawDisc *raw, int track, u32 relative, u8 *out) {
    RawTrack *t = &raw->track[track];
    if (relative < t->silence) {
        memset(out, 0, DISC_RAW_SECTOR);
        return true;
    }
    Sint64 pos = (Sint64)(t->offset + relative - t->silence) * DISC_RAW_SECTOR;
    return SDL_SeekIO(t->io, pos, SDL_IO_SEEK_SET) == pos &&
           SDL_ReadIO(t->io, out, DISC_RAW_SECTOR) == DISC_RAW_SECTOR;
}

static bool eq(const char *a, const char *b) {
    return a && SDL_strcasecmp(a, b) == 0;
}

/* Split a CUE line, preserving spaces inside quoted filenames. */
static char *token(char **rest) {
    char *p = *rest + strspn(*rest, " \t");
    if (!*p) {
        return NULL;
    }
    char *end;
    if (*p == '"') {
        p++;
        end = strchr(p, '"');
        if (!end) {
            return NULL;
        }
    } else {
        end = p + strcspn(p, " \t");
    }
    *rest = *end ? end + 1 : end;
    *end = '\0';
    return p;
}

static int timestamp(const char *s) {
    unsigned m, sec, f;
    char extra;
    if (!s || sscanf(s, "%3u:%2u:%2u%c", &m, &sec, &f, &extra) != 3 || sec >= 60 || f >= 75) {
        return -1;
    }
    return (int)((m * 60 + sec) * 75 + f);
}

static SDL_IOStream *open_track(const char *sheet, char *name) {
    /* FILE paths are relative to the sheet. Resolve only files within its directory. */
    for (char *p = name; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }
    if (!*name || *name == '/' || strchr(name, ':') || !strcmp(name, "..") ||
        !strncmp(name, "../", 3) || strstr(name, "/../") ||
        (strlen(name) >= 3 && !strcmp(name + strlen(name) - 3, "/.."))) {
        return NULL;
    }
#ifdef __ANDROID__
    if (SDL_strncmp(sheet, "content://", 10) == 0) {
        JNIEnv *env = SDL_GetAndroidJNIEnv();
        if ((*env)->PushLocalFrame(env, 8) < 0)
            return NULL;
        jobject activity = SDL_GetAndroidActivity();
        jclass cls = (*env)->GetObjectClass(env, activity);
        jmethodID resolve = (*env)->GetMethodID(
            env, cls, "resolveTrack", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        jstring uri = resolve ? (*env)->CallObjectMethod(env, activity, resolve,
                                                         (*env)->NewStringUTF(env, sheet),
                                                         (*env)->NewStringUTF(env, name))
                              : NULL;
        SDL_IOStream *io = NULL;
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        } else if (uri) {
            const char *path = (*env)->GetStringUTFChars(env, uri, NULL);
            if (path) {
                io = SDL_IOFromFile(path, "rb");
                (*env)->ReleaseStringUTFChars(env, uri, path);
            }
        }
        (*env)->PopLocalFrame(env, NULL);
        return io;
    }
#endif
    const char *base = sheet + strlen(sheet);
    while (base > sheet && base[-1] != '/' && base[-1] != '\\')
        base--;
    char *path = NULL;
    if (SDL_asprintf(&path, "%.*s%s", (int)(base - sheet), sheet, name) < 0)
        return NULL;
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    SDL_free(path);
    return io;
}

static bool metadata(const char *key) {
    return eq(key, "REM") || eq(key, "TITLE") || eq(key, "PERFORMER") || eq(key, "FLAGS") ||
           eq(key, "CATALOG") || eq(key, "ISRC") || eq(key, "SONGWRITER") || eq(key, "CDTEXTFILE");
}

static DiscCheck parse_sheet(RawDisc *raw, const char *path, char *text) {
    int count = 0;
    SDL_IOStream *file = NULL;
    u32 next = 0;
    bool indexed = false, index0 = false;
    char *save = NULL;
    if (strncmp(text, "\xef\xbb\xbf", 3) == 0)
        text += 3;
    for (char *line = SDL_strtok_r(text, "\r\n", &save); line;
         line = SDL_strtok_r(NULL, "\r\n", &save)) {
        char *key = token(&line);
        if (!key || metadata(key))
            continue;
        char *arg = token(&line);
        char *value = token(&line);
        if (eq(key, "FILE")) {
            if (!arg || !eq(value, "BINARY") || raw->file_count == TRACKS ||
                (file && (!indexed || SDL_GetIOSize(file) != (Sint64)next * DISC_RAW_SECTOR))) {
                return DISC_BAD_CUE;
            }
            file = open_track(path, arg);
            if (!file)
                return DISC_MISSING_TRACK;
            raw->files[raw->file_count++] = file;
            next = 0;
            indexed = false;
        } else if (eq(key, "TRACK")) {
            unsigned n;
            char extra;
            if (!file || count == TRACKS || (!indexed && next != 0) || !arg ||
                sscanf(arg, "%2u%c", &n, &extra) != 1 || n != (unsigned)count + 1 ||
                !eq(value, count ? "AUDIO" : "MODE2/2352"))
                return DISC_BAD_CUE;
            raw->track[count] = (RawTrack){file, next, 0};
            next += frames[count++];
            indexed = false;
            index0 = false;
        } else if (eq(key, "PREGAP")) {
            if (!count || indexed || index0 || raw->track[count - 1].io != file ||
                timestamp(arg) != AUDIO_GAP || raw->track[count - 1].silence)
                return DISC_BAD_CUE;
            if (count > 1) {
                raw->track[count - 1].silence = AUDIO_GAP;
                next -= AUDIO_GAP;
            }
        } else if (eq(key, "INDEX")) {
            if (!count || indexed || raw->track[count - 1].io != file)
                return DISC_BAD_CUE;
            RawTrack *t = &raw->track[count - 1];
            int pos = timestamp(value);
            if (eq(arg, "00") && !index0 && !t->silence && pos == (int)t->offset) {
                index0 = true;
            } else if (eq(arg, "01") &&
                       pos == (int)(t->offset + (count > 1 ? AUDIO_GAP : 0) - t->silence)) {
                indexed = true;
            } else {
                return DISC_BAD_CUE;
            }
        } else {
            return DISC_BAD_CUE;
        }
    }
    return count == TRACKS && indexed && SDL_GetIOSize(file) == (Sint64)next * DISC_RAW_SECTOR
               ? DISC_OK
               : DISC_BAD_CUE;
}

static bool usa_executable(RawDisc *raw) {
    /* PSX2.EXE on the US pressing. Check its complete ISO user payload, without reading
     * hundreds of MB of audio every time the start screen validates the selection. */
    u8 sector[DISC_RAW_SECTOR];
    u32 crc = 0;
    for (u32 i = 0; i < 239; i++) {
        if (!raw_disc_read(raw, 0, 17094 + i, sector))
            return false;
        crc = SDL_crc32(crc, sector + 24, DISC_DATA_SECTOR);
    }
    return crc == 0x76edf607u;
}

DiscCheck raw_disc_open(const char *path, RawDisc **out) {
    *out = NULL;
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io)
        return DISC_MISSING;
    RawDisc *raw = calloc(1, sizeof *raw);
    DiscCheck result = DISC_WRONG_IMAGE;
    Sint64 size = SDL_GetIOSize(io);
    if (raw && size > 0 && size <= MAX_CUE) {
        char *text = malloc((size_t)size + 1);
        if (text && SDL_ReadIO(io, text, (size_t)size) == (size_t)size &&
            !memchr(text, '\0', (size_t)size)) {
            text[size] = '\0';
            result = parse_sheet(raw, path, text);
        }
        free(text);
    } else if (raw) {
        u32 offset = 0;
        for (int i = 0; i < TRACKS; i++) {
            raw->track[i] = (RawTrack){io, offset, 0};
            offset += frames[i];
        }
        if (size == (Sint64)offset * DISC_RAW_SECTOR) {
            raw->files[raw->file_count++] = io;
            io = NULL;
            result = DISC_OK;
        }
    }
    if (io)
        SDL_CloseIO(io);
    if (result == DISC_OK && !usa_executable(raw))
        result = DISC_WRONG_IMAGE;
    if (result != DISC_OK) {
        raw_disc_close(raw);
        return result;
    }
    *out = raw;
    return DISC_OK;
}

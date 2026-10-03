#include "port/savestate.h"

#include "port/controls.h"
#include "port/runtime.h"

#include <SDL3/SDL.h>
#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATE_MAGIC 0x5453434Eu /* "NCST" */
#define STATE_VERSION 1u
#define MESSAGE_MS 2500

typedef struct StateHeader {
    u32 magic;
    u32 version;
    u32 raw_size;
    u32 frame;
} StateHeader;

static char state_dir[1100];
static int pending_save = -1, pending_load = -1;
static char message[128];
static unsigned loads_applied;
static Uint64 message_ns;
void (*savestate_after_load)(void);

void state_io(StateIO *io, void *data, size_t size) {
    if (io->error) {
        return;
    }
    if (io->loading) {
        if (io->pos + size > io->len) {
            io->error = true;
            return;
        }
        memcpy(data, io->buf + io->pos, size);
        io->pos += size;
        return;
    }
    if (io->len + size > io->cap) {
        size_t cap = io->cap ? io->cap * 2 : (size_t)8 << 20;
        while (cap < io->len + size) {
            cap *= 2;
        }
        u8 *nb = realloc(io->buf, cap);
        if (nb == NULL) {
            io->error = true;
            return;
        }
        io->buf = nb;
        io->cap = cap;
    }
    memcpy(io->buf + io->len, data, size);
    io->len += size;
}

static void serialize_all(StateIO *io) {
    cpu_serialize(io);
    bios_serialize(io);
    dispatch_serialize(io);
    irq_serialize(io);
    timers_serialize(io);
    dma_serialize(io);
    gpu_serialize(io);
    spu_serialize(io);
    cdrom_serialize(io);
    mdec_serialize(io);
}

static void set_message(const char *fmt, int slot) {
    SDL_snprintf(message, sizeof message, fmt, slot + 1);
    message_ns = SDL_GetTicksNS();
    NC_LOG("savestate: %s", message);
}

const char *savestate_message(void) {
    if (message[0] == '\0' || SDL_GetTicksNS() - message_ns > (Uint64)MESSAGE_MS * 1000000u) {
        return NULL;
    }
    return message;
}

static void slot_path(int slot, char *out, size_t cap) {
    SDL_snprintf(out, cap, "%sslot%d.state", state_dir, slot + 1);
}

void savestate_init(const char *dir) {
    SDL_snprintf(state_dir, sizeof state_dir, "%sstates/", dir);
    SDL_CreateDirectory(state_dir);
}

void savestate_request_save(int slot) {
    pending_save = slot;
}

void savestate_request_load(int slot) {
    if (!savestate_exists(slot)) {
        set_message("Slot %d is empty", slot);
        return;
    }
    pending_load = slot;
}

bool savestate_exists(int slot) {
    char path[1200];
    slot_path(slot, path, sizeof path);
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static void do_save(int slot) {
    controls_unpatch_camera(); /* never store free-look's temporary camera values */
    StateIO io = {0};
    serialize_all(&io);
    if (io.error) {
        set_message("Save to slot %d failed (out of memory)", slot);
        free(io.buf);
        return;
    }
    mz_ulong packed = mz_compressBound((mz_ulong)io.len);
    u8 *out = malloc(sizeof(StateHeader) + packed);
    int rc = out ? mz_compress2(out + sizeof(StateHeader), &packed, io.buf, (mz_ulong)io.len, 1)
                 : MZ_MEM_ERROR;
    free(io.buf);
    if (rc != MZ_OK) {
        free(out);
        set_message("Save to slot %d failed (compression)", slot);
        return;
    }
    extern unsigned nc_frame_count;
    StateHeader hdr = {STATE_MAGIC, STATE_VERSION, (u32)io.len, nc_frame_count};
    memcpy(out, &hdr, sizeof hdr);
    char path[1200];
    slot_path(slot, path, sizeof path);
    bool ok = SDL_SaveFile(path, out, sizeof hdr + packed);
    free(out);
    set_message(ok ? "Saved state %d" : "Save to slot %d failed (disk)", slot);
}

static void do_load(int slot) {
    char path[1200];
    slot_path(slot, path, sizeof path);
    size_t size = 0;
    u8 *file = SDL_LoadFile(path, &size);
    StateHeader hdr;
    if (file == NULL || size < sizeof hdr) {
        SDL_free(file);
        set_message("Load of slot %d failed (unreadable)", slot);
        return;
    }
    memcpy(&hdr, file, sizeof hdr);
    if (hdr.magic != STATE_MAGIC || hdr.version != STATE_VERSION) {
        SDL_free(file);
        set_message("Slot %d is from an incompatible version", slot);
        return;
    }
    StateIO io = {.loading = true, .len = hdr.raw_size};
    io.buf = malloc(hdr.raw_size);
    mz_ulong got = hdr.raw_size;
    int rc = io.buf ? mz_uncompress(io.buf, &got, file + sizeof hdr, (mz_ulong)(size - sizeof hdr))
                    : MZ_MEM_ERROR;
    SDL_free(file);
    if (rc != MZ_OK || got != hdr.raw_size) {
        free(io.buf);
        set_message("Load of slot %d failed (corrupt)", slot);
        return;
    }
    controls_reset();
    serialize_all(&io);
    free(io.buf);
    if (io.error || io.pos != io.len) {
        /* A partially applied state cannot be trusted; this only happens with a broken file. */
        NC_FATAL("save state slot %d is truncated", slot + 1);
    }
    if (savestate_after_load != NULL) {
        savestate_after_load();
    }
    loads_applied++;
    extern unsigned nc_frame_count;
    NC_LOG("savestate: restored frame %u (saved at %u)", nc_frame_count, hdr.frame);
    set_message("Loaded state %d", slot);
}

/* Testing aid: NC_STATE_TEST=S,L,D saves to slot 5 at VBlank S, loads it back once at VBlank L,
 * and dumps RAM to build/ram/state_<D>_<pass>.bin when VBlank D is reached (once before the
 * load, once after). The two dumps are identical if restoring is exact. */
static void state_test(void) {
    extern unsigned nc_frame_count;
    static int state = -1;
    static unsigned s_at, l_at, d_at, pass;
    if (state < 0) {
        const char *env = getenv("NC_STATE_TEST");
        state = env != NULL && sscanf(env, "%u,%u,%u", &s_at, &l_at, &d_at) == 3 ? 1 : 0;
    }
    if (state == 0) {
        return;
    }
    if (state == 1 && nc_frame_count >= s_at) {
        savestate_request_save(SAVESTATE_SLOTS - 1);
        state = 2;
    } else if (state == 2 && nc_frame_count >= l_at) {
        savestate_request_load(SAVESTATE_SLOTS - 1);
        state = 3;
    }
    if (nc_frame_count >= d_at && pass < 2 && (pass == 0 || loads_applied > 0)) {
        char path[64];
        SDL_snprintf(path, sizeof path, "build/ram/state_%u_%u.bin", d_at, pass);
        SDL_CreateDirectory("build/ram");
        SDL_SaveFile(path, nc_ram, RAM_SIZE);
        NC_LOG("state test: dump pass %u at frame %u", pass, nc_frame_count);
        pass++;
    }
}

void nc_hook_savestate_point(CPUState *c) {
    (void)c;
    state_test();
    if (pending_save >= 0) {
        int slot = pending_save;
        pending_save = -1;
        do_save(slot);
    }
    if (pending_load >= 0) {
        int slot = pending_load;
        pending_load = -1;
        do_load(slot);
    }
}

/* High-level emulation of the PS1 BIOS functions the game uses (A0/B0/C0 tables). */
#include "port/bios.h"

#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/hw.h"
#include "port/memcard.h"
#include "port/runtime.h"

#include <setjmp.h>
#include <string.h>

enum { COP0_SR = 12 };

/* --- events --------------------------------------------------------------------------- */

#define EV_MAX 32
#define EV_DESC_BASE 0xF1000000u

enum {
    EV_ST_UNUSED = 0x0000,
    EV_ST_WAIT = 0x1000,
    EV_ST_ACTIVE = 0x2000,
    EV_ST_ALREADY = 0x4000,
};
enum { EV_MD_INTR = 0x1000, EV_MD_NOINTR = 0x2000 };

typedef struct Event {
    u32 cls, spec, mode, func, status;
} Event;

/* --- files (BIOS open/read/lseek/close over the disc) --------------------------------- */

#define FD_FIRST 2 /* 0 and 1 are the TTY */
#define FD_MAX 8

typedef struct OpenFile {
    bool used;
    DiscFile file;
    u32 pos;
    int card_port; /* -1 for disc files, else memory card slot (0/1) */
    char card_name[21];
    bool async;
} OpenFile;

/* Memory card event classes and specs (Psy-Q libapi). */
#define EV_HW_CARD 0xF0000011u
#define EV_SW_CARD 0xF4000001u
#define EV_SP_IOE 0x0004u
#define EV_SP_ERROR 0x8000u
#define EV_SP_TIMEOUT 0x0100u

/* firstfile/nextfile search state */
typedef struct CardSearch {
    int port;
    int block;
    char pattern[21];
} CardSearch;

/* --- state ---------------------------------------------------------------------------- */

typedef struct BiosState {
    Event events[EV_MAX];
    u32 hook; /* HookEntryInt buffer address, 0 = none */
    u32 pad_buf[2], pad_size[2];
    bool pad_started;
    u16 pad_buttons; /* active-high PS1 button bits, port 1 */
    OpenFile files[FD_MAX];
    CardSearch search;
} BiosState;

static BiosState bios;

static jmp_buf exc_env;
static CPUState exc_saved;
static bool in_exception;

/* Psy-Q patches BIOS routines through the B0/C0 function tables (e.g. _patch_gte, _patch_pad).
 * Under HLE those patches have no effect, so the tables live in otherwise-unused kernel RAM and
 * each entry points at a scratch area that the patch code can freely overwrite. */
#define FAKE_B0_TABLE 0x00000874u
#define FAKE_C0_TABLE 0x00000674u
#define FAKE_PATCH_AREA 0x00002000u

void bios_init(void) {
    memset(&bios, 0, sizeof bios);
    for (u32 i = 0; i < 0x60; i++) {
        MEM_W32(FAKE_B0_TABLE + i * 4, FAKE_PATCH_AREA + 0x4000u + i * 0x80u);
    }
    for (u32 i = 0; i < 0x20; i++) {
        MEM_W32(FAKE_C0_TABLE + i * 4, FAKE_PATCH_AREA + i * 0x200u);
    }
}

void bios_set_pad(u16 buttons) {
    bios.pad_buttons = buttons;
}

static u32 open_event(u32 cls, u32 spec, u32 mode, u32 func) {
    for (u32 i = 0; i < EV_MAX; i++) {
        if (bios.events[i].status == EV_ST_UNUSED) {
            bios.events[i] = (Event){cls, spec, mode, func, EV_ST_WAIT};
            return EV_DESC_BASE | i;
        }
    }
    NC_LOG("bios: out of event slots");
    return 0xFFFFFFFFu;
}

static Event *event_at(u32 desc) {
    u32 i = desc & 0xFFFF;
    return ((desc & 0xFFFF0000u) == EV_DESC_BASE && i < EV_MAX) ? &bios.events[i] : NULL;
}

void bios_deliver_event(CPUState *c, u32 cls, u32 spec) {
    for (u32 i = 0; i < EV_MAX; i++) {
        Event *e = &bios.events[i];
        if (e->status != EV_ST_ACTIVE || e->cls != cls || e->spec != spec) {
            continue;
        }
        if (e->mode == EV_MD_INTR && e->func != 0) {
            u32 saved[32];
            memcpy(saved, c->r, sizeof saved);
            nc_call(c, e->func);
            memcpy(c->r, saved, sizeof saved);
        } else {
            e->status = EV_ST_ALREADY;
        }
    }
}

/* --- string helpers --------------------------------------------------------------------- */

static void read_cstr(u32 addr, char *out, size_t cap) {
    size_t i = 0;
    for (; i + 1 < cap; i++) {
        char ch = (char)MEM_R8(addr + (u32)i);
        if (ch == '\0') {
            break;
        }
        out[i] = ch;
    }
    out[i] = '\0';
}

/* Minimal printf over guest memory: enough for the libraries' diagnostic messages. */
static void guest_printf(CPUState *c) {
    char fmt[256], out[512];
    read_cstr(c->r[4], fmt, sizeof fmt);
    u32 arg_regs[3] = {c->r[5], c->r[6], c->r[7]};
    unsigned argi = 0;
    size_t o = 0;
    for (const char *p = fmt; *p != '\0' && o + 32 < sizeof out; p++) {
        if (*p != '%') {
            out[o++] = *p;
            continue;
        }
        p++;
        while (*p == '-' || *p == '0' || (*p >= '1' && *p <= '9') || *p == 'l' || *p == 'h') {
            p++;
        }
        u32 arg = argi < 3 ? arg_regs[argi] : MEM_R32(c->r[29] + 0x10 + (argi - 3) * 4);
        argi++;
        switch (*p) {
        case 'd':
        case 'i':
            o += (size_t)snprintf(out + o, sizeof out - o, "%d", (int)arg);
            break;
        case 'u':
            o += (size_t)snprintf(out + o, sizeof out - o, "%u", arg);
            break;
        case 'x':
        case 'X':
        case 'p':
            o += (size_t)snprintf(out + o, sizeof out - o, "%x", arg);
            break;
        case 'c':
            out[o++] = (char)arg;
            break;
        case 's': {
            char str[128];
            read_cstr(arg, str, sizeof str);
            o += (size_t)snprintf(out + o, sizeof out - o, "%s", str);
            break;
        }
        case '%':
            out[o++] = '%';
            argi--;
            break;
        default:
            break;
        }
    }
    out[o] = '\0';
    NC_LOG("[guest] %s", out);
}

/* "bu00:NAME" -> slot 0, "bu10:NAME" -> slot 1; returns the slot or -1 if not a card path. */
static int card_path(const char *path, const char **name) {
    if (strncmp(path, "bu", 2) != 0 || path[2] < '0' || path[2] > '1' || path[4] != ':') {
        return -1;
    }
    *name = path + 5;
    return path[2] - '0';
}

static void card_event(CPUState *c, u32 spec) {
    bios_deliver_event(c, EV_SW_CARD, spec);
    bios_deliver_event(c, EV_HW_CARD, spec);
}

static u32 file_open(u32 name_addr, u32 mode) {
    char name[64];
    read_cstr(name_addr, name, sizeof name);
    const char *card_name;
    int port = card_path(name, &card_name);
    for (u32 fd = FD_FIRST; fd < FD_MAX; fd++) {
        if (bios.files[fd].used) {
            continue;
        }
        OpenFile *f = &bios.files[fd];
        if (port >= 0) {
            if (mode & 0x200) { /* FCREAT: size in blocks in the upper half */
                if (!memcard_create(port, card_name, mode >> 16)) {
                    NC_LOG("bios: cannot create %s", name);
                    return 0xFFFFFFFFu;
                }
            } else if (memcard_file_size(port, card_name) < 0) {
                return 0xFFFFFFFFu;
            }
            memset(f, 0, sizeof *f);
            f->used = true;
            f->card_port = port;
            strncpy(f->card_name, card_name, 20);
            f->async = (mode & 0x8000) != 0; /* FASYNC: completion is signalled by event */
            return fd;
        }
        if (!disc_find(name, &f->file)) {
            NC_LOG("bios: open(%s) failed", name);
            return 0xFFFFFFFFu;
        }
        f->used = true;
        f->pos = 0;
        f->card_port = -1;
        return fd;
    }
    return 0xFFFFFFFFu;
}

/* Reads/writes a memory card file through a host buffer. */
static u32 card_rw(CPUState *c, OpenFile *f, u32 addr, u32 len, bool write) {
    static u8 buf[0x20000];
    len = len < sizeof buf ? len : (u32)sizeof buf;
    if (write) {
        for (u32 i = 0; i < len; i++) {
            buf[i] = (u8)MEM_R8(addr + i);
        }
    }
    int n = memcard_rw(f->card_port, f->card_name, f->pos, buf, len, write);
    if (n < 0) {
        card_event(c, EV_SP_ERROR);
        return 0xFFFFFFFFu;
    }
    if (!write) {
        for (int i = 0; i < n; i++) {
            MEM_W8(addr + (u32)i, buf[i]);
        }
    }
    f->pos += (u32)n;
    if (f->async) {
        card_event(c, EV_SP_IOE);
    }
    return (u32)n;
}

/* Fills a BIOS DIRENTRY (name[20], attr, size, next, head, system[4]) for a card file. */
static u32 card_next(u32 dirent) {
    char name[21];
    u32 size;
    int block = memcard_find(bios.search.port, bios.search.pattern, bios.search.block, name, &size);
    if (block < 0) {
        return 0;
    }
    bios.search.block = block;
    for (u32 i = 0; i < 20; i++) {
        MEM_W8(dirent + i, (u8)name[i]);
    }
    MEM_W32(dirent + 0x14, 0x50);
    MEM_W32(dirent + 0x18, size);
    MEM_W32(dirent + 0x1C, 0);
    MEM_W32(dirent + 0x20, (u32)block);
    MEM_W32(dirent + 0x24, 0);
    return dirent;
}

static u32 file_read(u32 fd, u32 dst, u32 len) {
    if (fd >= FD_MAX || !bios.files[fd].used) {
        return 0xFFFFFFFFu;
    }
    OpenFile *f = &bios.files[fd];
    u8 sector[DISC_DATA_SECTOR];
    u32 done = 0;
    while (done < len && f->pos < f->file.size) {
        if (!disc_read_data(f->file.lba + f->pos / DISC_DATA_SECTOR, sector)) {
            break;
        }
        u32 off = f->pos % DISC_DATA_SECTOR;
        u32 n = DISC_DATA_SECTOR - off;
        if (n > len - done) {
            n = len - done;
        }
        if (n > f->file.size - f->pos) {
            n = f->file.size - f->pos;
        }
        for (u32 i = 0; i < n; i++) {
            MEM_W8(dst + done + i, sector[off + i]);
        }
        done += n;
        f->pos += n;
    }
    return done;
}

/* Psy-Q's printf writes one character at a time; collect output into lines. */
static u32 tty_write(u32 src, u32 len) {
    static char line[256];
    static size_t used;
    for (u32 i = 0; i < len; i++) {
        char ch = (char)MEM_R8(src + i);
        if (ch == '\n' || used == sizeof line - 1) {
            line[used] = '\0';
            NC_LOG("[tty] %s", line);
            used = 0;
        } else if (ch != '\r') {
            line[used++] = ch;
        }
    }
    return len;
}

/* --- exceptions / interrupts ------------------------------------------------------------- */

/* Interrupt-time BIOS work done before the game's hook: the pad driver. */
static void bios_irq_chain(CPUState *c) {
    (void)c;
    if ((irq_read(0) & 1u) && bios.pad_started) {
        for (int port = 0; port < 2; port++) {
            u32 buf = bios.pad_buf[port];
            if (buf == 0 || bios.pad_size[port] < 4) {
                continue;
            }
            if (port == 0) {
                u16 raw = (u16)~bios.pad_buttons;
                MEM_W8(buf + 0, 0x00);    /* status: ok */
                MEM_W8(buf + 1, 0x41);    /* digital pad, 1 halfword of data */
                MEM_W8(buf + 2, (u8)raw); /* buttons, active low */
                MEM_W8(buf + 3, (u8)(raw >> 8));
            } else {
                MEM_W8(buf + 0, 0xFF); /* no controller */
            }
        }
    }
}

void bios_exception(CPUState *c) {
    if (in_exception) {
        return;
    }
    in_exception = true;
    exc_saved = *c;

    bios_irq_chain(c);

    if (bios.hook != 0 && setjmp(exc_env) == 0) {
        /* Resume the hook's setjmp() as if it returned 1 (Psy-Q: "if (setjmp(b)) trapIntr();").
         * Interrupts stay disabled while the handler runs. */
        u32 h = bios.hook;
        c->r[31] = MEM_R32(h + 0x00);
        c->r[29] = MEM_R32(h + 0x04);
        c->r[30] = MEM_R32(h + 0x08);
        for (int i = 0; i < 8; i++) {
            c->r[16 + i] = MEM_R32(h + 0x0C + (u32)i * 4);
        }
        c->r[28] = MEM_R32(h + 0x2C);
        c->r[2] = 1;
        c->cop0[COP0_SR] &= ~0x401u;
        nc_call(c, c->r[31]);
        NC_LOG("bios: interrupt hook returned without ReturnFromException");
    }

    s32 budget = c->budget;
    *c = exc_saved;
    c->budget = budget;
    in_exception = false;
}

static _Noreturn void return_from_exception(void) {
    longjmp(exc_env, 1);
}

bool bios_in_exception(void) {
    return in_exception;
}

/* --- dispatch --------------------------------------------------------------------------- */

static void bios_a0(CPUState *c, u32 fn) {
    u32 a0 = c->r[4], a1 = c->r[5];
    switch (fn) {
    case 0x39: /* InitHeap(addr, size): malloc is not used by the game */
    case 0x44: /* FlushCache */
    case 0x70: /* _bu_init */
    case 0x71: /* _96_init */
    case 0x72: /* _96_remove */
        break;
    case 0x3F:
        guest_printf(c);
        break;
    case 0x42: { /* Load(filename, headerbuf) */
        char name[64];
        ExecInfo info;
        read_cstr(a0, name, sizeof name);
        if (exe_load(name, &info)) {
            exe_write_info(&info, a1);
            c->r[2] = 1;
        } else {
            c->r[2] = 0;
        }
        return;
    }
    case 0x43: { /* Exec(headerbuf, argc, argv) */
        ExecInfo info;
        exe_read_info(a0, &info);
        c->r[2] = exe_exec(c, &info, a1, c->r[6]);
        return;
    }
    case 0x49: /* GPU_cw(word) */
        gpu_gp0(a0);
        break;
    case 0xAB: /* _card_info(port): port 0x00 = slot 1, 0x10 = slot 2 */
    case 0xAC: /* _card_load(port) */
        card_event(c, memcard_present((int)(a0 >> 4) & 1) ? EV_SP_IOE : EV_SP_TIMEOUT);
        c->r[2] = 1;
        return;
    default:
        NC_FATAL("BIOS A0:%02X not implemented (ra=0x%08X)", fn, c->r[31]);
    }
    c->r[2] = 0;
}

static void bios_b0(CPUState *c, u32 fn) {
    u32 a0 = c->r[4], a1 = c->r[5], a2 = c->r[6], a3 = c->r[7];
    Event *e;
    switch (fn) {
    case 0x07: /* DeliverEvent(class, spec) */
        bios_deliver_event(c, a0, a1);
        break;
    case 0x08: /* OpenEvent(class, spec, mode, func) */
        c->r[2] = open_event(a0, a1, a2, a3);
        return;
    case 0x09: /* CloseEvent(desc) */
        if ((e = event_at(a0)) != NULL) {
            e->status = EV_ST_UNUSED;
        }
        c->r[2] = 1;
        return;
    case 0x0A: /* WaitEvent(desc) */
    case 0x0B: /* TestEvent(desc) */
        if ((e = event_at(a0)) != NULL && e->status == EV_ST_ALREADY) {
            e->status = EV_ST_ACTIVE;
            c->r[2] = 1;
        } else {
            c->r[2] = 0;
        }
        return;
    case 0x0C: /* EnableEvent(desc) */
        if ((e = event_at(a0)) != NULL && e->status != EV_ST_UNUSED) {
            e->status = EV_ST_ACTIVE;
        }
        c->r[2] = 1;
        return;
    case 0x0D: /* DisableEvent(desc) */
        if ((e = event_at(a0)) != NULL && e->status != EV_ST_UNUSED) {
            e->status = EV_ST_WAIT;
        }
        c->r[2] = 1;
        return;
    case 0x12: /* InitPAD(buf1, size1, buf2, size2) */
        bios.pad_buf[0] = a0;
        bios.pad_size[0] = a1;
        bios.pad_buf[1] = a2;
        bios.pad_size[1] = a3;
        c->r[2] = 1;
        return;
    case 0x13: /* StartPAD */
    case 0x15: /* OutdatedPadInitAndStart */
        /* Like the real BIOS, this also leaves the critical section that Psy-Q's _patch_pad
         * entered, re-enabling interrupts. */
        bios.pad_started = true;
        c->cop0[COP0_SR] |= 0x401u;
        c->r[2] = 1;
        return;
    case 0x14: /* StopPAD */
        bios.pad_started = false;
        break;
    case 0x17: /* ReturnFromException */
        if (in_exception) {
            return_from_exception();
        }
        NC_LOG("bios: ReturnFromException outside an exception");
        break;
    case 0x18: /* ResetEntryInt */
        bios.hook = 0;
        break;
    case 0x19: /* HookEntryInt(buf) */
        bios.hook = a0;
        break;
    case 0x32: /* open(name, mode) */
        c->r[2] = file_open(a0, a1);
        return;
    case 0x33: /* lseek(fd, offset, whence) */
        if (a0 < FD_MAX && bios.files[a0].used) {
            OpenFile *f = &bios.files[a0];
            f->pos = a2 == 0 ? a1 : (a2 == 1 ? f->pos + a1 : f->file.size + a1);
            c->r[2] = f->pos;
        } else {
            c->r[2] = 0xFFFFFFFFu;
        }
        return;
    case 0x34: /* read(fd, dst, len) */
        if (a0 < FD_MAX && bios.files[a0].used && bios.files[a0].card_port >= 0) {
            c->r[2] = card_rw(c, &bios.files[a0], a1, a2, false);
        } else {
            c->r[2] = file_read(a0, a1, a2);
        }
        return;
    case 0x35: /* write(fd, src, len): the TTY or a memory card file */
        if (a0 <= 1) {
            c->r[2] = tty_write(a1, a2);
        } else if (a0 < FD_MAX && bios.files[a0].used && bios.files[a0].card_port >= 0) {
            c->r[2] = card_rw(c, &bios.files[a0], a1, a2, true);
        } else {
            c->r[2] = 0xFFFFFFFFu;
        }
        return;
    case 0x36: /* close(fd) */
        if (a0 < FD_MAX) {
            bios.files[a0].used = false;
        }
        c->r[2] = a0;
        return;
    case 0x41: { /* format(name) */
        char path[64];
        const char *rest;
        read_cstr(a0, path, sizeof path);
        int port = card_path(path, &rest);
        if (port >= 0 && memcard_present(port)) {
            memcard_format(port);
            c->r[2] = 1;
        } else {
            c->r[2] = 0;
        }
        return;
    }
    case 0x42: { /* firstfile(pattern, direntry) */
        char path[64];
        const char *pattern;
        read_cstr(a0, path, sizeof path);
        int port = card_path(path, &pattern);
        if (port < 0) {
            c->r[2] = 0;
            return;
        }
        bios.search.port = port;
        bios.search.block = 0;
        strncpy(bios.search.pattern, *pattern ? pattern : "*", 20);
        bios.search.pattern[20] = '\0';
        c->r[2] = card_next(a1);
        return;
    }
    case 0x43: /* nextfile(direntry) */
        c->r[2] = card_next(a0);
        return;
    case 0x45: { /* erase(name) */
        char path[64];
        const char *name;
        read_cstr(a0, path, sizeof path);
        int port = card_path(path, &name);
        c->r[2] = port >= 0 && memcard_erase(port, name) ? 1 : 0;
        return;
    }
    case 0x4E:   /* _card_write(port, sector, src) */
    case 0x4F: { /* _card_read(port, sector, dst) */
        int port = (int)(a0 >> 4) & 1;
        u8 frame[128];
        bool ok;
        if (fn == 0x4E) {
            for (u32 i = 0; i < 128; i++) {
                frame[i] = (u8)MEM_R8(a2 + i);
            }
            ok = memcard_write_frame(port, a1, frame);
        } else {
            ok = memcard_read_frame(port, a1, frame);
            for (u32 i = 0; ok && i < 128; i++) {
                MEM_W8(a2 + i, frame[i]);
            }
        }
        card_event(c, ok ? EV_SP_IOE : EV_SP_TIMEOUT);
        c->r[2] = 1;
        return;
    }
    case 0x50: /* _new_card */
        break;
    case 0x4B: /* StartCARD: also re-enables interrupts, like StartPAD */
        c->cop0[COP0_SR] |= 0x401u;
        c->r[2] = 1;
        return;
    case 0x4A: /* InitCARD */
    case 0x4C: /* StopCARD */
        c->r[2] = 1;
        return;
    case 0x56: /* GetC0Table */
        c->r[2] = FAKE_C0_TABLE;
        return;
    case 0x57: /* GetB0Table */
        c->r[2] = FAKE_B0_TABLE;
        return;
    case 0x5B: /* ChangeClearPad(int) */
        break;
    default:
        NC_FATAL("BIOS B0:%02X not implemented (ra=0x%08X)", fn, c->r[31]);
    }
    c->r[2] = 0;
}

static void bios_c0(CPUState *c, u32 fn) {
    switch (fn) {
    case 0x0A: /* ChangeClearRCnt(timer, flag): returns the old flag */
        c->r[2] = 1;
        return;
    default:
        NC_FATAL("BIOS C0:%02X not implemented (ra=0x%08X)", fn, c->r[31]);
    }
}

void bios_call(CPUState *c, u32 table) {
    u32 fn = c->r[9];
    switch (table) {
    case 0xA0:
        bios_a0(c, fn);
        break;
    case 0xB0:
        bios_b0(c, fn);
        break;
    default:
        bios_c0(c, fn);
        break;
    }
}

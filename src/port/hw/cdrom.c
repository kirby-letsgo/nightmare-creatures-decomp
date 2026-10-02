/* CD-ROM controller (0x1F801800-0x1F801803), driven by Psy-Q LIBCD.
 * Reference: psx-spx "CDROM Controller". Responses are scheduled in emulated cycles and
 * delivered one interrupt at a time, as on hardware. */
#include "port/disc.h"
#include "port/hw/hw.h"
#include "port/runtime.h"

#include <string.h>

#define SECTOR_CYCLES_1X (PSX_CPU_HZ / 75u)
#define ACK_DELAY 25000u
#define SEEK_DELAY 150000u
#define INIT_DELAY 120000u
#define PAUSE_DELAY 80000u

enum { INT_DATA = 1, INT_COMPLETE = 2, INT_ACK = 3, INT_END = 4, INT_ERROR = 5 };

enum {
    STAT_ERROR = 0x01,
    STAT_MOTOR = 0x02,
    STAT_READING = 0x20,
    STAT_SEEKING = 0x40,
    STAT_PLAYING = 0x80,
};

enum {
    MODE_CDDA = 0x01,
    MODE_AUTOPAUSE = 0x02,
    MODE_REPORT = 0x04,
    MODE_XA_FILTER = 0x08,
    MODE_SIZE_2340 = 0x20,
    MODE_XA_ADPCM = 0x40,
    MODE_DOUBLE = 0x80,
};

#define FIFO_SIZE 16
#define QUEUE_SIZE 8

typedef struct Response {
    u8 irq;
    u8 len;
    u8 bytes[FIFO_SIZE];
    u64 due; /* emulated cycle at which it becomes visible */
} Response;

typedef enum DriveState { DRIVE_IDLE, DRIVE_READING, DRIVE_PLAYING } DriveState;

typedef struct CdState {
    u8 index;
    u8 params[FIFO_SIZE];
    u8 nparams;
    u8 int_enable, int_flag;
    u8 resp[FIFO_SIZE];
    u8 resp_len, resp_pos;
    Response queue[QUEUE_SIZE];
    u8 queued;

    u8 stat, mode;
    u8 filter_file, filter_chan;
    bool muted;
    u32 setloc; /* target LBA from Setloc */
    bool setloc_pending;
    u32 pos;      /* LBA of the next sector to read/play */
    u32 last_lba; /* LBA of the last sector delivered (for GetlocL/P) */
    DriveState drive;
    u64 next_sector;
    u32 sectors_since_report;

    u8 sector[DISC_RAW_SECTOR]; /* last sector read */
    u8 data[DISC_RAW_SECTOR];   /* data FIFO contents */
    u32 data_len, data_pos;
    bool data_ready; /* a sector is waiting to be requested */
} CdState;

/* The BIOS leaves all CD interrupts enabled after booting from the disc. */
static CdState cd = {.stat = STAT_MOTOR, .int_enable = 0x1F};
static u64 now; /* last cycle count seen by cdrom_tick */

static u8 bcd(u32 v) {
    return (u8)(((v / 10) << 4) | (v % 10));
}

static u32 unbcd(u8 v) {
    return (u32)(v >> 4) * 10u + (v & 0xFu);
}

static u32 msf_to_lba(u8 m, u8 s, u8 f) {
    u32 lba = (unbcd(m) * 60u + unbcd(s)) * 75u + unbcd(f);
    return lba >= 150u ? lba - 150u : 0u;
}

static void lba_to_msf(u32 lba, u8 out[3]) {
    lba += 150u;
    out[0] = bcd(lba / (60u * 75u));
    out[1] = bcd((lba / 75u) % 60u);
    out[2] = bcd(lba % 75u);
}

static void push(u8 irq, u64 delay, const u8 *bytes, u8 len) {
    if (cd.queued == QUEUE_SIZE) {
        NC_LOG("cdrom: response queue full, dropping INT%u", irq);
        return;
    }
    Response *r = &cd.queue[cd.queued++];
    r->irq = irq;
    r->len = len;
    memcpy(r->bytes, bytes, len);
    u64 due = now + delay;
    /* Keep responses in order. */
    if (cd.queued > 1 && cd.queue[cd.queued - 2].due > due) {
        due = cd.queue[cd.queued - 2].due;
    }
    r->due = due;
}

static void push_stat(u8 irq, u64 delay) {
    push(irq, delay, &cd.stat, 1);
}

static void push_error(u8 code) {
    u8 bytes[2] = {(u8)(cd.stat | STAT_ERROR), code};
    push(INT_ERROR, ACK_DELAY, bytes, 2);
}

/* Moves the oldest due response into the response FIFO once the previous one is acked. */
static void deliver(void) {
    if (cd.int_flag != 0 || cd.queued == 0 || cd.queue[0].due > now) {
        return;
    }
    Response r = cd.queue[0];
    memmove(&cd.queue[0], &cd.queue[1], (size_t)(cd.queued - 1) * sizeof(Response));
    cd.queued--;
    memcpy(cd.resp, r.bytes, r.len);
    cd.resp_len = r.len;
    cd.resp_pos = 0;
    cd.int_flag = r.irq;
    if (cd.int_flag & cd.int_enable) {
        irq_raise(IRQ_CDROM);
    }
}

static u64 sector_period(void) {
    return (cd.mode & MODE_DOUBLE) ? SECTOR_CYCLES_1X / 2 : SECTOR_CYCLES_1X;
}

static void start_drive(DriveState state) {
    if (cd.setloc_pending) {
        cd.pos = cd.setloc;
        cd.setloc_pending = false;
    }
    cd.drive = state;
    cd.stat = (u8)((cd.stat & ~(STAT_READING | STAT_PLAYING)) |
                   (state == DRIVE_READING ? STAT_READING : STAT_PLAYING));
    cd.next_sector = now + SEEK_DELAY + sector_period();
    cd.sectors_since_report = 0;
}

static void stop_drive(void) {
    cd.drive = DRIVE_IDLE;
    cd.stat &= (u8) ~(STAT_READING | STAT_PLAYING | STAT_SEEKING);
}

static int track_of(u32 lba) {
    for (int t = disc_track_count(); t >= 1; t--) {
        if (lba >= disc_track(t)->start) {
            return t;
        }
    }
    return 1;
}

static void command(u8 cmd) {
    u8 *p = cd.params;
    u8 out[8];
    switch (cmd) {
    case 0x01: /* Getstat */
        push_stat(INT_ACK, ACK_DELAY);
        break;
    case 0x02: /* Setloc(amm, ass, asect) */
        cd.setloc = msf_to_lba(p[0], p[1], p[2]);
        cd.setloc_pending = true;
        push_stat(INT_ACK, ACK_DELAY);
        break;
    case 0x03: /* Play([track]) */
        if (cd.nparams > 0 && p[0] != 0 && disc_track((int)unbcd(p[0])) != NULL) {
            cd.setloc = disc_track((int)unbcd(p[0]))->index1;
            cd.setloc_pending = true;
        }
        push_stat(INT_ACK, ACK_DELAY);
        start_drive(DRIVE_PLAYING);
        break;
    case 0x06: /* ReadN */
    case 0x1B: /* ReadS */
        push_stat(INT_ACK, ACK_DELAY);
        start_drive(DRIVE_READING);
        break;
    case 0x07: /* MotorOn */
        cd.stat |= STAT_MOTOR;
        push_stat(INT_ACK, ACK_DELAY);
        push_stat(INT_COMPLETE, INIT_DELAY);
        break;
    case 0x08: /* Stop */
        stop_drive();
        push_stat(INT_ACK, ACK_DELAY);
        cd.stat &= (u8)~STAT_MOTOR;
        push_stat(INT_COMPLETE, INIT_DELAY);
        break;
    case 0x09: /* Pause */
        push_stat(INT_ACK, ACK_DELAY);
        stop_drive();
        push_stat(INT_COMPLETE, PAUSE_DELAY);
        break;
    case 0x0A: /* Init */
        cd.mode = 0;
        stop_drive();
        cd.stat = STAT_MOTOR;
        push_stat(INT_ACK, ACK_DELAY);
        push_stat(INT_COMPLETE, INIT_DELAY);
        break;
    case 0x0B: /* Mute */
    case 0x0C: /* Demute */
        cd.muted = cmd == 0x0B;
        push_stat(INT_ACK, ACK_DELAY);
        break;
    case 0x0D: /* Setfilter(file, channel) */
        cd.filter_file = p[0];
        cd.filter_chan = p[1];
        push_stat(INT_ACK, ACK_DELAY);
        break;
    case 0x0E: /* Setmode(mode) */
        cd.mode = p[0];
        push_stat(INT_ACK, ACK_DELAY);
        break;
    case 0x0F: /* Getparam */
        out[0] = cd.stat;
        out[1] = cd.mode;
        out[2] = 0;
        out[3] = cd.filter_file;
        out[4] = cd.filter_chan;
        push(INT_ACK, ACK_DELAY, out, 5);
        break;
    case 0x10: /* GetlocL: header + subheader of the last data sector */
        push(INT_ACK, ACK_DELAY, cd.sector + 12, 8);
        break;
    case 0x11: { /* GetlocP */
        int t = track_of(cd.last_lba);
        const DiscTrack *tr = disc_track(t);
        u32 rel = cd.last_lba >= tr->index1 ? cd.last_lba - tr->index1 : tr->index1 - cd.last_lba;
        out[0] = bcd((u32)t);
        out[1] = cd.last_lba >= tr->index1 ? 1 : 0;
        lba_to_msf(rel >= 150 ? rel - 150 : 0, out + 2);
        lba_to_msf(cd.last_lba, out + 5);
        push(INT_ACK, ACK_DELAY, out, 8);
        break;
    }
    case 0x13: /* GetTN */
        out[0] = cd.stat;
        out[1] = 0x01;
        out[2] = bcd((u32)disc_track_count());
        push(INT_ACK, ACK_DELAY, out, 3);
        break;
    case 0x14: { /* GetTD(track): start of track (0 = lead-out), as mm:ss */
        u32 t = cd.nparams ? unbcd(p[0]) : 0;
        u8 msf[3];
        if (t > (u32)disc_track_count()) {
            push_error(0x10);
            break;
        }
        lba_to_msf(t == 0 ? disc_leadout() : disc_track((int)t)->index1, msf);
        out[0] = cd.stat;
        out[1] = msf[0];
        out[2] = msf[1];
        push(INT_ACK, ACK_DELAY, out, 3);
        break;
    }
    case 0x15: /* SeekL */
    case 0x16: /* SeekP */
        stop_drive();
        if (cd.setloc_pending) {
            cd.pos = cd.setloc;
            cd.setloc_pending = false;
        }
        cd.last_lba = cd.pos;
        push_stat(INT_ACK, ACK_DELAY);
        push_stat(INT_COMPLETE, SEEK_DELAY);
        break;
    case 0x19: /* Test(sub) */
        if (cd.nparams && p[0] == 0x20) {
            const u8 version[4] = {0x94, 0x09, 0x19, 0xC0};
            push(INT_ACK, ACK_DELAY, version, 4);
        } else {
            push_error(0x10);
        }
        break;
    case 0x1A: { /* GetID: licensed NTSC-U data disc */
        const u8 id[8] = {STAT_MOTOR, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'};
        push_stat(INT_ACK, ACK_DELAY);
        push(INT_COMPLETE, INIT_DELAY, id, 8);
        break;
    }
    case 0x1E: /* ReadTOC */
        push_stat(INT_ACK, ACK_DELAY);
        push_stat(INT_COMPLETE, INIT_DELAY);
        break;
    default:
        NC_LOG("cdrom: unsupported command 0x%02X", cmd);
        push_error(0x40);
        break;
    }
    cd.nparams = 0;
}

static bool xa_matches(const u8 *raw) {
    if (!(cd.mode & MODE_XA_FILTER)) {
        return true;
    }
    return raw[16] == cd.filter_file && raw[17] == cd.filter_chan;
}

static void read_sector(void) {
    if (!disc_read_raw(cd.pos, cd.sector)) {
        stop_drive();
        push(INT_END, 0, &cd.stat, 1);
        return;
    }
    cd.last_lba = cd.pos++;

    if (cd.drive == DRIVE_PLAYING) {
        if (!cd.muted) {
            spu_cdda_feed(cd.sector);
        }
        int t = track_of(cd.last_lba);
        if ((cd.mode & MODE_AUTOPAUSE) && cd.pos >= disc_track(t)->start + disc_track(t)->frames) {
            stop_drive();
            push(INT_END, 0, &cd.stat, 1);
            return;
        }
        if ((cd.mode & MODE_REPORT) && ++cd.sectors_since_report >= 10) {
            cd.sectors_since_report = 0;
            u8 rep[8];
            const DiscTrack *tr = disc_track(t);
            u32 rel = cd.last_lba - (cd.last_lba >= tr->index1 ? tr->index1 : tr->start);
            rep[0] = cd.stat;
            rep[1] = bcd((u32)t);
            rep[2] = 1;
            lba_to_msf(rel >= 150 ? rel - 150 : 0, rep + 3);
            rep[6] = 0;
            rep[7] = 0;
            push(INT_DATA, 0, rep, 8);
        }
        return;
    }

    /* XA real-time audio sectors go to the SPU instead of the CPU. */
    u8 submode = cd.sector[18];
    if ((cd.mode & MODE_XA_ADPCM) && (submode & 0x04) && (submode & 0x40)) {
        if (xa_matches(cd.sector) && !cd.muted) {
            spu_xa_feed(cd.sector);
        }
        return;
    }

    cd.data_ready = true;
    push(INT_DATA, 0, &cd.stat, 1);
}

void cdrom_tick(u64 cycles) {
    now = cycles;
    while (cd.drive != DRIVE_IDLE && now >= cd.next_sector) {
        cd.next_sector += sector_period();
        read_sector();
    }
    deliver();
}

u8 cdrom_read(u32 reg) {
    switch (reg) {
    case 0: {
        u8 status = cd.index;
        status |= cd.nparams == 0 ? 0x08 : 0;           /* parameter FIFO empty */
        status |= cd.nparams < FIFO_SIZE ? 0x10 : 0;    /* parameter FIFO not full */
        status |= cd.resp_pos < cd.resp_len ? 0x20 : 0; /* response FIFO not empty */
        status |= cd.data_pos < cd.data_len ? 0x40 : 0; /* data FIFO not empty */
        return status;
    }
    case 1:
        return cd.resp_pos < cd.resp_len ? cd.resp[cd.resp_pos++] : 0;
    case 2:
        return cd.data_pos < cd.data_len ? cd.data[cd.data_pos++] : 0;
    default:
        return (cd.index & 1) ? (u8)(cd.int_flag | 0xE0) : (u8)(cd.int_enable | 0xE0);
    }
}

void cdrom_write(u32 reg, u8 value) {
    if (reg == 0) {
        cd.index = value & 3;
        return;
    }
    switch (reg * 4 + cd.index) {
    case 1 * 4 + 0:
        command(value);
        break;
    case 2 * 4 + 0:
        if (cd.nparams < FIFO_SIZE) {
            cd.params[cd.nparams++] = value;
        }
        break;
    case 2 * 4 + 1:
        cd.int_enable = value & 0x1F;
        break;
    case 3 * 4 + 0: /* request register: bit 7 = load the data FIFO */
        if ((value & 0x80) && cd.data_ready) {
            if (cd.mode & MODE_SIZE_2340) {
                memcpy(cd.data, cd.sector + 12, 2340);
                cd.data_len = 2340;
            } else {
                memcpy(cd.data, cd.sector + 24, 2048);
                cd.data_len = 2048;
            }
            cd.data_pos = 0;
            cd.data_ready = false;
        } else if (!(value & 0x80)) {
            cd.data_len = cd.data_pos = 0;
        }
        break;
    case 3 * 4 + 1: /* interrupt flag acknowledge */
        cd.int_flag &= (u8) ~(value & 0x1F);
        if (cd.int_flag == 0) {
            cd.resp_len = cd.resp_pos = 0;
        }
        if (value & 0x40) {
            cd.nparams = 0;
        }
        deliver();
        break;
    default:
        /* Audio volume / sound map registers: not needed for disc access. */
        break;
    }
}

u32 cdrom_dma_read(u8 *dst, u32 bytes) {
    u32 n = 0;
    while (n < bytes) {
        dst[n++] = cd.data_pos < cd.data_len ? cd.data[cd.data_pos++] : 0;
    }
    return n;
}

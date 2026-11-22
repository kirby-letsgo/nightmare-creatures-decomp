#include "decomp/pad.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include "port/runtime.h"

static u8 before[RAM_SIZE], after[RAM_SIZE];
static const u32 gp = 0x80120000, packet = 0x8008f1c0, config = 0x80130000;

static CPUState initial(void) {
    CPUState cpu = {0};
    for (unsigned i = 1; i < 32; ++i)
        cpu.r[i] = i * 0x13579u;
    cpu.r[28] = gp;
    cpu.r[29] = 0x801ff000;
    cpu.r[31] = 0x80010000;
    cpu.budget = 4096;
    return cpu;
}

static bool compare(CPUState cpu, u32 entry, bool (*native)(CPUState *), int cutoff) {
    memcpy(before, nc_ram, RAM_SIZE);
    CPUState actual = cpu;
    actual.budget = cutoff;
    CPUState low = actual;
    if (native(&low) || memcmp(&actual, &low, sizeof low) || memcmp(nc_ram, before, RAM_SIZE))
        return false;
    actual = cpu;
    if (!native(&actual))
        return false;
    memcpy(after, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
    interp_call(&cpu, entry);
    if (memcmp(&actual, &cpu, sizeof cpu) || memcmp(nc_ram, after, RAM_SIZE)) {
        fprintf(stderr, "%08x budget native/disc %d/%d, packet %02x/%04x\n", entry, actual.budget,
                cpu.budget, MEM_R8(packet + 1), MEM_R16(packet + 2));
        return false;
    }
    return true;
}

static bool ports(void) {
    const u8 types[] = {0xff, 0x41, 0x73, 0x53};
    for (unsigned a = 0; a < 4; ++a)
        for (unsigned b = 0; b < 4; ++b) {
            MEM_W8(packet, a ? 0 : 0xff);
            MEM_W8(packet + 1, types[a]);
            MEM_W8(packet + 34, b ? 0x80 : 0xff);
            MEM_W8(packet + 35, types[b]);
            MEM_W32(gp + 0x238, 0xabcdffff);
            if (!compare(initial(), 0x8006ff34, nc_select_pad, 70))
                return false;
            const unsigned mask = (a == 1 || a == 2) + 2 * (b == 1 || b == 2);
            if (MEM_R16(gp + 0x238) != mask || MEM_R16(gp + 0x23a) != (mask & 1    ? 0
                                                                       : mask == 2 ? 1
                                                                                   : 0xabcd))
                return false;
        }
    return true;
}

static bool decode(u32 type, u32 pressed, bool both, u32 flags, u8 x, u8 y, u8 right) {
    CPUState cpu = initial();
    cpu.r[2] = cpu.r[3] = 0;
    cpu.r[31] = 0x800705d8;
    MEM_W32(gp + 0x238, both ? 3 : 1);
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(config + 0x1a, 0xc0c04040);
    MEM_W32(config + 0x1e, 0xe0e02020);
    MEM_W32(packet, ((u32)(u16)~pressed << 16) | type << 8);
    MEM_W8(packet + 4, right);
    MEM_W8(packet + 6, x);
    MEM_W8(packet + 7, y);
    MEM_W32(packet + 34, 0x76544100);
    MEM_W8(gp + 0x410, 0x99);
    MEM_W8(gp + 0x414, 0x77);
    return compare(cpu, 0x800700fc, nc_decode_pad, 550);
}

/* Exercise the normal function entry with enough budget for both native
 * paths, then with enough to return but too little for the decoder shortcut. */
static bool caller(u32 budget, u8 first, u8 second, u32 argument, u32 flags, u32 state) {
    CPUState cpu = initial();
    cpu.r[4] = argument;
    cpu.r[5] = 1;
    cpu.budget = budget;
    MEM_W32(gp + 0x244, config);
    MEM_W32(config, flags);
    MEM_W32(config + 4, state);
    MEM_W32(gp + 0x2d0, 0);
    MEM_W32(0x8008ef30, 0x11223344);
    MEM_W32(packet, 0xf6ee4100u | first);
    MEM_W32(packet + 34, 0x12344100u | second);
    memcpy(before, nc_ram, RAM_SIZE);
    CPUState guest = cpu;
    nc_set_module(NC_MOD_NONE);
    interp_call(&guest, 0x80070050);
    memcpy(after, nc_ram, RAM_SIZE);
    memcpy(nc_ram, before, RAM_SIZE);
#ifdef NC_TEST_GENERATED_PAD
    nc_set_module(NC_MOD_PSX2);
    NcFunc read_pad = nc_lookup(0x80070050);
    if (!read_pad)
        return false;
    read_pad(&cpu);
    nc_set_module(NC_MOD_NONE);
#else
    CPUState input = cpu;
    u32 resume = nc_read_pad(&cpu);
    if (resume) {
        if (resume != 0x80070050 || memcmp(&cpu, &input, sizeof cpu) ||
            memcmp(before, nc_ram, RAM_SIZE))
            return false;
        interp_call(&cpu, resume);
    }
#endif
    bool notify = first == 0xff && argument && !(flags & 0xc0009840) && !(state & 0x03000040);
    if (MEM_R32(0x8008ef30) != (notify ? 0x10u : 0x11223344u) ||
        MEM_R32(config + 4) != (notify ? state | 0x40 : state))
        return false;
    u32 expected = first == 0xff ? 0 : 0x0911u | (second == 0xff ? 0 : 0xedcb0000u);
    return cpu.r[2] == expected && !memcmp(&cpu, &guest, sizeof cpu) &&
           !memcmp(nc_ram, after, RAM_SIZE);
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX2.EXE;1", &exe))
        return 1;
    bool ok = ports();
    for (unsigned i = 0; ok && i < 18; ++i) {
        u32 pressed = i == 16 ? 0 : i == 17 ? 0xfafa : 1u << i;
        ok = decode(0x41, pressed, false, 0, 128, 128, 128) &&
             decode(0x41, pressed, true, 0, 128, 128, 128);
    }
    const u8 axes[] = {0, 31, 32, 33, 63, 64, 128, 192, 193, 223, 224, 225, 255};
    const u8 right[] = {15, 16, 240, 241};
    const u32 flags[] = {0, 0x00100000, 0x40000000};
    for (unsigned i = 0; ok && i < sizeof axes; ++i)
        for (unsigned j = 0; ok && j < 4; ++j)
            for (unsigned k = 0; ok && k < 3; ++k)
                ok = decode(0x73, j * 2, true, flags[k], axes[i], axes[sizeof axes - 1 - i],
                            right[j]);
    ok = ok && decode(0x53, 0x9876, false, 0, 0, 0, 0);
    for (unsigned i = 0; ok && i < 4; ++i) {
        u8 first = i & 1 ? 0xff : 0, second = i & 2 ? 0xff : 0;
        ok = caller(4096, first, second, 0x800d7c04, 0x40000000, 0) &&
             caller(150, first, second, 0x800d7c04, 0x40000000, 0) &&
             caller(4096, first, second, 0, 0, 0) &&
             caller(4096, first, second, 0x800d7c04, 0, 0x03000000) &&
             caller(4096, first, second, 0x800d7c04, 0, 0);
    }
    disc_close();
    return ok ? 0 : 1;
}

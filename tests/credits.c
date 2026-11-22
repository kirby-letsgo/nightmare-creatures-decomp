#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

static u8 original_ram[RAM_SIZE], original_scratch[SCRATCH_SIZE];

static bool memory_unchanged(void) {
    return !memcmp(nc_ram, original_ram, sizeof original_ram) &&
           !memcmp(nc_scratch, original_scratch, sizeof original_scratch);
}

static bool check_input(u8 status, u8 id, u16 buttons, bool use_disc) {
    const u32 packet = 0x80024CA4;
    MEM_W8(packet, status);
    MEM_W8(packet + 1, id);
    MEM_W16(packet + 2, buttons);
    memcpy(original_ram, nc_ram, sizeof original_ram);
    memcpy(original_scratch, nc_scratch, sizeof original_scratch);

    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = i * 0x12345u;
    initial.hi = 11;
    initial.lo = 22;
    initial.r[31] = 0x80010000;
    initial.budget = 34;
    CPUState native = initial;
    if (nc_credits_skip_input(&native) || memcmp(&native, &initial, sizeof initial) ||
        !memory_unchanged())
        return false;

    native.budget = ++initial.budget;
    const bool supported = (id >> 4) == 4 || (id >> 4) == 7;
    const bool skip = status != 0xff && supported && (~buttons & 0x4008);
    if (!nc_credits_skip_input(&native) || native.r[2] != (u32)skip || !memory_unchanged())
        return false;
    if (use_disc) {
        CPUState guest = initial;
        interp_call(&guest, 0x80018C8C);
        if (memcmp(&guest, &native, sizeof guest) || !memory_unchanged()) {
            fprintf(stderr, "pad %02x/%02x/%04x, guest/native budget %d/%d\n", status, id, buttons,
                    guest.budget, native.budget);
            return false;
        }
    }
    return true;
}

static bool check_defaults(u32 record, u32 pointer_slot) {
    CPUState start = {0};
    for (unsigned i = 1; i < 32; i++)
        start.r[i] = i * 0x13579u;
    start.r[28] = pointer_slot - 8;
    start.r[31] = 0x80010000;
    start.budget = 75;
    memset(nc_fast_ptr(record), 0xa5, 0x30);
    MEM_W32(pointer_slot, record);
    memcpy(original_ram, nc_ram, sizeof original_ram);
    memcpy(original_scratch, nc_scratch, sizeof original_scratch);
    CPUState native = start;
    if (nc_credits_init_shared_record(&native) || memcmp(&native, &start, sizeof start) ||
        !memory_unchanged())
        return false;
    native.budget = ++start.budget;
    if (!nc_credits_init_shared_record(&native))
        return false;
    static u8 after_ram[RAM_SIZE], after_scratch[SCRATCH_SIZE];
    memcpy(after_ram, nc_ram, sizeof after_ram);
    memcpy(after_scratch, nc_scratch, sizeof after_scratch);
    memcpy(nc_ram, original_ram, sizeof original_ram);
    memcpy(nc_scratch, original_scratch, sizeof original_scratch);
    CPUState guest = start;
    interp_call(&guest, 0x80019220);
    return !memcmp(&guest, &native, sizeof native) &&
           !memcmp(nc_ram, after_ram, sizeof after_ram) &&
           !memcmp(nc_scratch, after_scratch, sizeof after_scratch);
}

int main(int argc, char **argv) {
    const bool use_disc = argc == 2;
    if (use_disc) {
        FILE *file = fopen(argv[1], "rb");
        if (!file)
            return 77;
        fclose(file);
        ExecInfo exe;
        if (!disc_open(argv[1]) || !exe_load("cdrom:\\CREDITS.EXE;1", &exe))
            return 1;
    }

    const u8 statuses[] = {0xff, 0, 1};
    const u8 ids[] = {0x41, 0x73, 0x4f, 0x70, 0x12, 0xff};
    const u16 buttons[] = {0xffff, 0xbfff, 0xfff7, 0xbff7, 0xefff, 0};
    for (unsigned s = 0; s < sizeof statuses; s++)
        for (unsigned i = 0; i < sizeof ids; i++)
            for (unsigned b = 0; b < sizeof buttons / sizeof buttons[0]; b++)
                if (!check_input(statuses[s], ids[i], buttons[b], use_disc))
                    return 1;
    if (use_disc) {
        if (!check_defaults(0x801fff80, 0x80024c70) ||
            !check_defaults(SCRATCH_BASE + 0x80, 0x80024c70) ||
            !check_defaults(0x80048000, 0xa0048000) || !check_defaults(0x80048000, 0x80048008))
            return 1;
        disc_close();
    }
    return 0;
}

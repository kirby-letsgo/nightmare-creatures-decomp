#include "decomp/credits.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/interp.h"
#include <stdio.h>

static u8 initial_ram[RAM_SIZE], guest_ram[RAM_SIZE];
static u8 initial_scratch[SCRATCH_SIZE], guest_scratch[SCRATCH_SIZE];

static bool check(u32 left, u32 right, u16 control, u32 sp) {
    const u32 spu = 0x1f801c00;
    CPUState initial = {0};
    for (unsigned i = 1; i < 32; i++)
        initial.r[i] = i * 0x16379u;
    initial.r[4] = left;
    initial.r[5] = right;
    initial.r[29] = sp;
    initial.r[31] = 0x80010000;
    initial.budget = 14;
    memset(nc_fast_ptr(sp - 0x50), 0xa5, 0x60);
    memcpy(initial_ram, nc_ram, sizeof initial_ram);
    memcpy(initial_scratch, nc_scratch, sizeof initial_scratch);
    CPUState native = initial;
    if (nc_credits_set_cd_volume(&native) || memcmp(&native, &initial, sizeof native) ||
        memcmp(nc_ram, initial_ram, sizeof initial_ram) ||
        memcmp(nc_scratch, initial_scratch, sizeof initial_scratch))
        return false;

    initial.budget = 4096;
    CPUState guest = initial;
    MEM_W16(spu + 0x1aa, control);
    MEM_W16(spu + 0x1b0, 0x1234);
    MEM_W16(spu + 0x1b2, 0xabcd);
    interp_call(&guest, 0x800192e4);
    const u16 cd_left = MEM_R16(spu + 0x1b0), cd_right = MEM_R16(spu + 0x1b2);
    const u16 cd_control = MEM_R16(spu + 0x1aa);
    if (cd_left != (u16)((left << 7) - 1) || cd_right != (u16)((right << 7) - 1) ||
        cd_control != (u16)(control | 1))
        return false;
    memcpy(guest_ram, nc_ram, sizeof guest_ram);
    memcpy(guest_scratch, nc_scratch, sizeof guest_scratch);

    memcpy(nc_ram, initial_ram, sizeof initial_ram);
    memcpy(nc_scratch, initial_scratch, sizeof initial_scratch);
    MEM_W16(spu + 0x1aa, control);
    MEM_W16(spu + 0x1b0, 0x1234);
    MEM_W16(spu + 0x1b2, 0xabcd);
    native = initial;
    return nc_credits_set_cd_volume(&native) && !memcmp(&guest, &native, sizeof guest) &&
           !memcmp(nc_ram, guest_ram, sizeof guest_ram) &&
           !memcmp(nc_scratch, guest_scratch, sizeof guest_scratch) &&
           cd_left == MEM_R16(spu + 0x1b0) && cd_right == MEM_R16(spu + 0x1b2) &&
           cd_control == MEM_R16(spu + 0x1aa);
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\CREDITS.EXE;1", &exe))
        return 1;
    const u32 levels[] = {0, 1, 0x7f, 0x80, 0xffff, 0x80000001};
    for (unsigned i = 0; i < sizeof levels / sizeof levels[0]; i++)
        for (unsigned j = 0; j < sizeof levels / sizeof levels[0]; j++)
            if (!check(levels[i], levels[j], (i & 1) ? 0x4001 : 0x4002,
                       (j & 1) ? SCRATCH_BASE + 0x200 : 0x801fef00)) {
                fprintf(stderr, "CD volume mismatch for %08x/%08x\n", levels[i], levels[j]);
                return 1;
            }
    disc_close();
    return 0;
}
